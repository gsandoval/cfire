#pragma once
// tests/unit/test_cf188.hpp — CF-188: every stdlib type is real, documented in cfire terms, and shown by hover.

#include "../test_harness.hpp"
#include "../process_run.hpp"

namespace cfire_test_cf188 {
inline auto frame(std::string_view body) -> std::string { return std::format("Content-Length: {}\r\n\r\n{}", body.size(), body); }

// The hover markdown the server returns for `line:character` (0-based) in `src`, or "" for none.
inline auto hover_at(const std::filesystem::path& dir, std::string_view src, int line, int character) -> std::string {
    const auto file = dir / "main.cf";
    { std::ofstream(file) << src; }
    const std::string uri = cfire::lsp::detail::path_to_uri(file);
    std::stringstream in, out;
    in << frame(std::format(R"({{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"{}","capabilities":{{}}}}}})",
                            cfire::lsp::detail::json_escape(cfire::lsp::detail::path_to_uri(dir))));
    in << frame(std::format(R"({{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{{"textDocument":{{"uri":"{}","languageId":"cfire","version":1,"text":"{}"}}}}}})",
                            cfire::lsp::detail::json_escape(uri), cfire::lsp::detail::json_escape(src)));
    in << frame(std::format(R"({{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{{"textDocument":{{"uri":"{}"}},"position":{{"line":{},"character":{}}}}}}})",
                            cfire::lsp::detail::json_escape(uri), line, character));
    in << frame(R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":null})");
    in << frame(R"({"jsonrpc":"2.0","method":"exit","params":null})");
    cfire::lsp::Server srv(10);
    srv.handle_stream(in, out);
    const std::string resp = out.str();
    const auto at = resp.find(R"("id":2)");
    if (at == std::string::npos) return "";
    const auto value = resp.find(R"("value":")", at);
    if (value == std::string::npos) return "";
    const auto end = resp.find(R"("}})", value);
    return cfire::lsp::detail::json_unescape(std::string_view(resp).substr(value + 9, end == std::string::npos ? std::string::npos : end - value - 9));
}
} // namespace cfire_test_cf188

void test_cf188_stdlib_type_surface(const std::filesystem::path& rt_dir) {
    std::println("--- Running CF-188 stdlib type surface (catalog docs, hover, every type usable) ---");
    (void)rt_dir;

    // 1. The catalog passes the doc audit `cfire doc --check` applies: every type and function documented in cfire terms.
    for (const auto& problem : cfire::doc::catalog_doc_problems()) require(false, "CF-188 catalog: " + problem);

    // 2. Every listed type works as a parameter type (it used to reach clang for half of them). `build.*` types exist
    //    only in build.cf and must say so instead.
    struct Probe {
        std::string import_path, spelled;
        bool manifest = false;
    };
    std::vector<Probe> probes;
    for (const auto& mod : cfire::doc::stdlib_catalog()) {
        for (const auto& td : mod.type_docs) {
            std::string spelled = std::format("{}.{}", mod.ns_alias, td.bare_name());
            const auto br = td.name.find('[');
            if (br != std::string::npos) {
                const auto params = std::count(td.name.begin() + static_cast<std::ptrdiff_t>(br), td.name.end(), ',') + 1;
                spelled += params == 1 ? "[i32]" : "[str, i32]";
            }
            probes.push_back({mod.import_path, spelled, td.kind == "manifest"});
        }
    }
    require(probes.size() > 60, std::format("CF-188: expected the full stdlib type list, got {}", probes.size()));
    ScopedTempDir dir("cfire_cf188_types");
    const std::string cfire = std::filesystem::absolute(cfire_exe()).string();
    std::vector<std::string> failures(probes.size());
    parallel_for_each_index(probes.size(), default_jobs(), [&](std::size_t i) {
        const auto& p = probes[i];
        auto sub = dir.subpath(std::format("t{}", i));
        (void)cfire::fs::write_text(sub / "main.cf", std::format("package main\nimport \"{}\"\nfn take(x: {}) {{\n}}\nfn main() {{\n}}\n", p.import_path, p.spelled));
        auto r = run_process({cfire, "check", "main.cf"}, sub, 300);
        const std::string all = r.out + r.err;
        if (p.manifest) {
            if (r.exit_code == 0 || !all.contains("cfire/UNKNOWN-STDLIB-TYPE") || !all.contains("build.cf"))
                failures[i] = std::format("{} outside build.cf must be cfire/UNKNOWN-STDLIB-TYPE, got: {}", p.spelled, all.substr(0, 300));
        } else if (r.exit_code != 0) {
            failures[i] = std::format("`fn take(x: {})` does not check: {}", p.spelled, all.substr(0, 300));
        }
    });
    for (const auto& f : failures) {
        if (!f.empty()) require(false, "CF-188: " + f);
    }

    // 3. Hover shows the type, a field, an associated function and an enum variant in cfire terms.
    {
        ScopedTempDir hdir("cfire_cf188_hover");
        const std::string src =
            "package main\nimport \"core:net/http\"\nimport \"core:log\"\n"
            "fn handle(req: http.Request) -> http.Response {\n"
            "    log.set_level(log.Level::Debug)\n"
            "    return http.Response.json(200, req.body)\n"
            "}\nfn main() {\n}\n";
        using cfire_test_cf188::hover_at;
        auto type_h = hover_at(hdir.path, src, 3, 22);
        require(type_h.contains("Struct `http.Request`") && type_h.contains("`body: str`") && type_h.contains("`headers: map[str]str`"),
                "CF-188 hover on http.Request lists its fields in cfire terms, got: " + type_h);
        auto field_h = hover_at(hdir.path, src, 5, 41);
        require(field_h.contains("Field `http.Request.body`") && field_h.contains("body: str"), "CF-188 hover on req.body, got: " + field_h);
        auto assoc_h = hover_at(hdir.path, src, 5, 27);
        require(assoc_h.contains("http.Response.json") && assoc_h.contains("-> Response"), "CF-188 hover on http.Response.json, got: " + assoc_h);
        auto variant_h = hover_at(hdir.path, src, 4, 29);
        require(variant_h.contains("Enum Variant `log.Level::Debug`"), "CF-188 hover on log.Level::Debug, got: " + variant_h);
        for (const auto& h : {type_h, field_h, assoc_h, variant_h}) {
            require(!h.contains("std::") && !h.contains("Router&"), "CF-188 hover never shows C++ types, got: " + h);
        }
    }

    // 4. Sema knows stdlib struct fields (no cfire/UNKNOWN-TYPE for `b := req.body`).
    require_sema_ok("package main\nimport \"core:net/http\"\nfn f(req: http.Request) -> str {\n    b := req.body\n    return b\n}\nfn main() {\n}\n");
}
