#pragma once
// tests/unit/test_audit_w0.hpp — Wave W0 regression net (CF-140, CF-157) from the 2026-11-11 language audit.

#include "../test_harness.hpp"
#include "../../src/mcp.hpp"
#include <fstream>
#include <future>

namespace cfire_test_w0 {

inline auto find_repo_dir(const std::filesystem::path& rt_dir, std::string_view rel) -> std::filesystem::path {
    std::filesystem::path p(rel);
    if (std::filesystem::exists(p)) return p;
    return rt_dir.parent_path() / p;
}

inline auto lsp_frame(std::string_view body) -> std::string {
    return std::format("Content-Length: {}\r\n\r\n{}", body.size(), body);
}

} // namespace cfire_test_w0

// CF-140: Sema checked lookups and LSP/MCP exception barriers.
void test_cf140_crash_proof_tools(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cf140_crash_proof_tools (CF-140)");
    const std::string repro =
        "package main\n"
        "import \"./pkg/nothere\"\n"
        "fn main() {\n"
        "    x := nothere.f()\n"
        "}\n";

    // 1. A missing local package is a cfire diagnostic, not an abort or a leaked clang error.
    {
        ScopedTempDir tmp("cfire_cf140_repro");
        auto main_cf = tmp.path / "main.cf";
        { std::ofstream(main_cf) << repro; }
        cfire::analysis::CheckOptions opts;
        opts.runtime_dir = rt_dir;
        bool threw = false;
        std::vector<cfire::analysis::Diagnostic> diags;
        try {
            diags = cfire::analysis::check_path(main_cf, opts);
        } catch (...) {
            threw = true;
        }
        require(!threw, "CF-140: check_path on a missing local package must not throw");
        bool has_unresolved = false;
        bool has_clang_leak = false;
        for (const auto& d : diags) {
            if (d.code == "cfire/UNRESOLVED-PACKAGE" && d.line == 2) has_unresolved = true;
            if (d.code == "cfire/TYPE-ERROR") has_clang_leak = true;
        }
        require(has_unresolved, "CF-140: expected cfire/UNRESOLVED-PACKAGE on line 2");
        require(!has_clang_leak, "CF-140: missing package must not fall through to a clang TYPE-ERROR");
    }

    // 2. Every mutation-fuzz crash seed from the audit produces diagnostics without throwing.
    {
        auto corpus = cfire_test_w0::find_repo_dir(rt_dir, "tests/fuzz/corpus/sema_crashes");
        require(std::filesystem::exists(corpus), "CF-140: tests/fuzz/corpus/sema_crashes exists");
        int seeds = 0;
        int threw = 0;
        cfire::analysis::CheckOptions opts;
        opts.runtime_dir = rt_dir;
        opts.verify_cpp_backend = false;
        for (const auto& entry : std::filesystem::directory_iterator(corpus)) {
            if (entry.path().extension() != ".cf") continue;
            ++seeds;
            ScopedTempDir tmp("cfire_cf140_seed");
            auto main_cf = tmp.path / "main.cf";
            std::filesystem::copy_file(entry.path(), main_cf);
            auto content = cfire::fs::read_text(main_cf);
            try {
                (void)cfire::analysis::check_source(main_cf.string(), content ? *content : "", opts);
            } catch (...) {
                ++threw;
                std::println(stderr, "  seed threw: {}", entry.path().filename().string());
            }
        }
        require(seeds >= 34, std::format("CF-140: expected >= 34 crash seeds, found {}", seeds));
        require(threw == 0, std::format("CF-140: {} crash seed(s) still throw from Sema", threw));
    }

    // 3. The LSP server survives the repro and keeps answering requests.
    {
        ScopedTempDir tmp("cfire_cf140_lsp");
        auto main_cf = tmp.path / "main.cf";
        { std::ofstream(main_cf) << repro; }
        std::string uri = cfire::lsp::detail::path_to_uri(main_cf);
        std::stringstream in, out;
        in << cfire_test_w0::lsp_frame(std::format(
            R"({{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"file://{}","capabilities":{{}}}}}})",
            cfire::lsp::detail::json_escape(tmp.path.string())));
        in << cfire_test_w0::lsp_frame(std::format(
            R"({{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{{"textDocument":{{"uri":"{}","languageId":"cfire","version":1,"text":"{}"}}}}}})",
            cfire::lsp::detail::json_escape(uri), cfire::lsp::detail::json_escape(repro)));
        in << cfire_test_w0::lsp_frame(std::format(
            R"({{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{{"textDocument":{{"uri":"{}"}},"position":{{"line":3,"character":10}}}}}})",
            cfire::lsp::detail::json_escape(uri)));
        in << cfire_test_w0::lsp_frame(std::format(
            R"({{"jsonrpc":"2.0","id":3,"method":"textDocument/documentSymbol","params":{{"textDocument":{{"uri":"{}"}}}}}})",
            cfire::lsp::detail::json_escape(uri)));
        in << cfire_test_w0::lsp_frame(R"({"jsonrpc":"2.0","id":4,"method":"shutdown","params":null})");
        in << cfire_test_w0::lsp_frame(R"({"jsonrpc":"2.0","method":"exit","params":null})");
        cfire::lsp::Server srv(10);
        bool threw = false;
        try {
            srv.handle_stream(in, out);
        } catch (...) {
            threw = true;
        }
        std::string resp = out.str();
        require(!threw, "CF-140: LSP handle_stream must not propagate exceptions");
        require(resp.contains(R"("id":3)") && resp.contains(R"("id":4)"),
            std::format("CF-140: LSP must keep answering after the missing-package file is opened, got: {}", resp.substr(0, 400)));
    }

    // 4. A throwing MCP tool reports isError instead of terminating the server.
    {
        auto srv = cfire::ai::make_mcp_server("cf140");
        srv.add_tool("boom", "always throws", R"({"type":"object"})",
            [](std::string_view) -> std::expected<std::string, cfire::Error> {
                throw std::runtime_error("kaboom");
            });
        std::string resp;
        bool threw = false;
        try {
            resp = srv.handle_jsonrpc(R"({"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"boom","arguments":{}}})");
        } catch (...) {
            threw = true;
        }
        require(!threw, "CF-140: MCP tools/call must not propagate tool exceptions");
        require(resp.contains(R"("isError":true)") && resp.contains("kaboom"),
            std::format("CF-140: expected isError result naming the exception, got: {}", resp));

        auto compiler_srv = cfire::mcp::build_compiler_mcp_server(rt_dir);
        std::string check_resp = compiler_srv.handle_jsonrpc(std::format(
            R"({{"jsonrpc":"2.0","id":10,"method":"tools/call","params":{{"name":"cfire_check","arguments":{{"code":"{}"}}}}}})",
            cfire::lsp::detail::json_escape(repro)));
        require(check_resp.contains(R"("id":10)"), "CF-140: cfire_check on the repro returns a response");
    }

    // 5. Source gate: Sema must not use throwing map lookups.
    {
        auto sema_dir = cfire_test_w0::find_repo_dir(rt_dir, "src/sema");
        int hits = 0;
        for (const auto& entry : std::filesystem::directory_iterator(sema_dir)) {
            auto text = cfire::fs::read_text(entry.path());
            if (!text) continue;
            std::size_t pos = 0;
            while ((pos = text->find(".at(", pos)) != std::string::npos) {
                ++hits;
                std::println(stderr, "  throwing lookup in {}", entry.path().filename().string());
                pos += 4;
            }
        }
        require(hits == 0, std::format("CF-140: found {} '.at(' call(s) in src/sema; use find()/lookup_or_empty()", hits));
    }
}

// CF-177: sync::context children must not leak through their parent, and deriving from an
// already-cancelled parent must not deadlock. Found by the CF-159 ThreadSanitizer job.
void test_cf177_context_lifetime([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cf177_context_lifetime (CF-177)");
    namespace ctx = cfire::sync::context;
    auto bg = ctx::background();

    // 1. A cancelled, out-of-scope child is freed (previously kept alive by the parent's callback).
    std::weak_ptr<ctx::ContextState> weak_child;
    {
        auto [c, cancel] = ctx::with_cancel(bg);
        weak_child = c.raw_state();
        cancel("done");
    }
    require(weak_child.expired(), "CF-177: cancelled child context is freed once out of scope");

    // 2. Request-scoped children never cancelled explicitly don't accumulate on a long-lived parent.
    for (int i = 0; i < 1000; ++i) {
        auto [c, cancel] = ctx::with_cancel(bg);
        auto [t, tcancel] = ctx::with_timeout(c, 60000);
        (void)t;
        (void)tcancel;
    }
    std::size_t pending = 0;
    {
        std::lock_guard<std::mutex> lk(bg.raw_state()->mu);
        pending = bg.raw_state()->callbacks.size();
    }
    require(pending == 0, std::format("CF-177: background() holds {} stale child callbacks after 1000 dropped children", pending));

    // 3. Deriving from an already-cancelled parent completes (previously self-deadlocked on the parent mutex).
    auto [p, pcancel] = ctx::with_cancel(bg);
    pcancel("stop");
    auto fut = std::async(std::launch::async, [p = p] {
        auto [c, cancel] = ctx::with_cancel(p);
        (void)cancel;
        return std::pair<bool, std::string>{c.is_cancelled(), c.reason()};
    });
    if (fut.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
        std::println("  [FAIL] CF-177: with_cancel on a cancelled parent deadlocked");
        std::fflush(stdout);
        std::_Exit(1);
    }
    auto [cancelled, reason] = fut.get();
    require(cancelled && reason == "stop", std::format("CF-177: child of cancelled parent inherits cancellation (got {} '{}')", cancelled, reason));
}

// CF-153: secure temporary storage. Private dirs are unique, 0700 and removed on scope exit, and no
// source file creates a predictable name under the shared temp directory.
void test_cf153_secure_temp_storage(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cf153_secure_temp_storage (CF-153)");
    std::filesystem::path kept;
    {
        cfire::detail::SecureTempDir a("cfire_cf153");
        cfire::detail::SecureTempDir b("cfire_cf153");
        require(a.ok() && b.ok(), "CF-153: SecureTempDir creates directories");
        require(a.path() != b.path(), "CF-153: SecureTempDir paths are unique");
        require(std::filesystem::is_directory(a.path()), "CF-153: SecureTempDir path is a directory");
#if !defined(_WIN32)
        auto perms = std::filesystem::status(a.path()).permissions();
        require((perms & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) == std::filesystem::perms::none,
            "CF-153: SecureTempDir is private (mode 0700)");
#endif
        { std::ofstream(a.file("x.txt")) << "secret"; }
        kept = a.path();
    }
    require(!std::filesystem::exists(kept), "CF-153: SecureTempDir is removed with its contents on scope exit");

    auto cache = cfire::detail::user_cache_dir("cf153-test");
    if (!cache.empty()) {
        require(std::filesystem::is_directory(cache), "CF-153: user_cache_dir creates the directory");
        auto tmp = std::filesystem::temp_directory_path();
        require(!std::filesystem::weakly_canonical(cache).string().starts_with(std::filesystem::weakly_canonical(tmp).string()),
            "CF-153: user_cache_dir is not under the shared temp directory");
        std::error_code ec;
        std::filesystem::remove(cache, ec);
    }

    // Source gate: no predictable names under the shared temp dir, and no hard-coded /tmp locations.
    int hits = 0;
    for (std::string_view sub : {"src", "runtime", "tests"}) {
        auto dir = cfire_test_w0::find_repo_dir(rt_dir, sub);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            if (ext != ".hpp" && ext != ".cpp" && ext != ".h") continue;
            if (entry.path().string().find("/.cfire/") != std::string::npos) continue;
            if (entry.path().filename() == "test_audit_w0.hpp") continue;  // contains the patterns below
            auto text = cfire::fs::read_text(entry.path());
            if (!text) continue;
            for (std::string_view pat : {"temp_directory_path() / \"", "std::filesystem::path(\"/tmp", "format(\"/tmp/"}) {
                if (text->find(pat) != std::string::npos) {
                    ++hits;
                    std::println(stderr, "  predictable temp path ({}) in {}", pat, entry.path().string());
                }
            }
        }
    }
    require(hits == 0, std::format("CF-153: {} source file(s) create predictable temp paths; use cfire::detail::SecureTempDir", hits));
}
