#pragma once
// tests/unit/test_ext_soundness_cache_and_clean.hpp — Regression tests for canonical ext:* imports,
// fail-closed SSH, ext_smoke catalog coverage, unified OS/XDG cache, safe `cfire clean`, and MCP parity.

#include "../test_harness.hpp"
#include "../conformance_runner.hpp"
#include "../process_run.hpp"
#include "../../src/build/cache_paths.hpp"
#include "../../src/build/clean.hpp"
#include "../../src/mcp.hpp"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#if __has_include(<print>)
#include <print>
#endif
#include <string>
#include <string_view>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace test_ext_soundness_detail {

struct ScopedEnv {
    std::string key;
    bool had_old = false;
    std::string old_val;

    ScopedEnv(std::string k, const std::string& val, bool set_val = true) : key(std::move(k)) {
        if (const char* cur = std::getenv(key.c_str())) {
            had_old = true;
            old_val = cur;
        }
        if (set_val) {
            cfire::build::set_process_env(key.c_str(), val);
        } else {
            cfire::build::unset_process_env(key.c_str());
        }
    }

    ~ScopedEnv() {
        if (had_old) {
            cfire::build::set_process_env(key.c_str(), old_val);
        } else {
            cfire::build::unset_process_env(key.c_str());
        }
    }
};

inline void write_file_checked(const std::filesystem::path& p, std::string_view content) {
    std::error_code ec;
    if (!p.parent_path().empty()) {
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    auto wr = cfire::fs::write_text(p, content);
    require(wr.has_value(), std::format("failed to write test file {}", p.string()));
}

} // namespace test_ext_soundness_detail

void test_ext_soundness_cache_and_clean(const std::filesystem::path& rt_dir) {
    std::println("--- Running ext:* soundness, unified cache, safe clean & MCP parity tests ---");
    const std::string cfire = std::filesystem::absolute(cfire_exe()).string();

    // 1. Canonical module paths: zero m.aliases in catalog, INVALID-IMPORT + cfire fix for every retired alias
    {
        for (const auto& mod : cfire::doc::stdlib_catalog()) {
            require(mod.aliases.empty(),
                std::format("stdlib catalog module '{}' must have zero aliases", mod.import_path));
        }

        ScopedTempDir fix_tmp("cfire_canon_fix");
        auto f = fix_tmp.file("aliases.cf",
            "package main\n"
            "import \"ext:ssh\" as s\n"
            "import \"ext:sodium\" as n\n"
            "import \"ext:sqlite\" as q\n"
            "import \"ext:zstd\" as z\n"
            "import \"ext:compress\" as c\n"
            "import \"ext:ai/agent\" as a\n"
            "import \"ext:tako\" as t\n"
            "import \"core:http\" as h\n"
            "import \"core:aes\" as ae\n"
            "import \"core:webui\" as wu\n"
            "fn main() {}\n"
        );
        auto chk = run_process({cfire, "check", "--json", f.string()}, fix_tmp.path, 60);
        require(chk.exit_code != 0 && chk.out.contains("cfire/INVALID-IMPORT"),
            "non-canonical imports fail cfire check with cfire/INVALID-IMPORT");

        auto fix_res = run_process({cfire, "fix", f.string()}, fix_tmp.path, 60);
        require(fix_res.exit_code == 0, std::format("cfire fix rewrites retired aliases (rc={})", fix_res.exit_code));
        auto fixed = cfire::fs::read_text(f).value_or("");
        for (std::string_view expected : {
            "import \"ext:net/ssh\" as s",
            "import \"ext:crypto/sodium\" as n",
            "import \"ext:db/sqlite\" as q",
            "import \"ext:compress/zstd\" as z",
            "import \"ext:compress/zstd\" as c",
            "import \"ext:ai\" as a",
            "import \"core:tako\" as t",
            "import \"core:net/http\" as h",
            "import \"core:crypto/aes\" as ae",
            "import \"ext:webui\" as wu"
        }) {
            require(fixed.contains(expected), std::format("cfire fix produced '{}'", expected));
        }
        auto rechk = run_process({cfire, "check", f.string()}, fix_tmp.path, 60);
        require(rechk.exit_code == 0, "fixed file passes cfire check cleanly");
    }

    // 2. ext:net/ssh fail-closed behavior & compile-time rejection without libssh outside CFIRE_TEST_MODE
    {
        test_ext_soundness_detail::ScopedEnv unset_headless("CFIRE_HEADLESS_TEST", "", false);
        cfire::ssh::enable_mock_server(false);
        require(!cfire::ssh::detail::is_mock_mode(),
            "ssh::detail::is_mock_mode() is false without enable_mock_server() or CFIRE_HEADLESS_TEST");

        std::uint16_t closed_port = 1;
        {
            auto ln = cfire::net::listen_tcp_addr("127.0.0.1", 0);
            if (ln) closed_port = ln->port();
        }

        auto conn = cfire::ssh::connect("127.0.0.1", closed_port, "deploy", 400);
        require(!conn.has_value(), "ssh::connect to closed port fails closed with Err (never a mock session)");

        if (!conformance::host_has_optional_lib("libssh")) {
            ScopedTempDir ssh_dir("cfire_ssh_nolib");
            auto main_cf = ssh_dir.file("main.cf",
                "package main\n"
                "import \"ext:net/ssh\"\n"
                "fn main() {\n"
                "    _ = ssh.connect(\"127.0.0.1\", 1, \"deploy\", 200) or { return }\n"
                "}\n"
            );
            auto bres = run_process({cfire, "build", main_cf.string()}, ssh_dir.path, 60);
            const std::string combined = bres.out + bres.err;
            require(bres.exit_code != 0 && combined.contains("libssh"),
                std::format("cfire build importing ext:net/ssh fails when libssh is absent (got rc={}, out={})",
                    bres.exit_code, combined));
        }
    }

    // 3. ext_smoke catalog completeness audit
    {
        auto smoke_dir = rt_dir.parent_path() / "tests" / "conformance" / "ext_smoke";
        for (const auto& mod : cfire::doc::stdlib_catalog()) {
            if (!mod.import_path.starts_with("ext:")) continue;
            auto cf_path = smoke_dir / (mod.ns_alias + ".cf");
            require(std::filesystem::exists(cf_path),
                std::format("ext_smoke fixture exists for {} ({})", mod.import_path, cf_path.string()));
            auto src = cfire::fs::read_text(cf_path).value_or("");
            for (const auto& fn : mod.functions) {
                std::string needle;
                if (fn.name.ends_with(".make")) {
                    auto tname = fn.name.substr(0, fn.name.size() - 5);
                    needle = mod.ns_alias + "." + tname;
                } else if (auto dot = fn.name.find('.'); dot != std::string::npos) {
                    const std::string mname = fn.name.substr(dot + 1);
                    needle = "." + mname + "(";
                    if (!src.contains(needle) && src.contains("." + mname + "[")) {
                        needle = "." + mname + "[";
                    } else if (!src.contains(needle) && src.contains("." + mname + " |")) {
                        needle = "." + mname + " |";
                    }
                } else {
                    needle = mod.ns_alias + "." + fn.name;
                }
                require(src.contains(needle),
                    std::format("ext_smoke/{}.cf exercises catalog entry {} (expected '{}')",
                        mod.ns_alias, fn.name, needle));
            }
        }
    }

    // 4. Unified OS/XDG user cache root, .cfire-cache marker & foreign-dir safety
    {
        ScopedTempDir cache_tmp("cfire_cache_unify");
        auto fake_home = cache_tmp.path / "home";
        auto fake_xdg = cache_tmp.path / "xdg";
        auto fake_override = cache_tmp.path / "override_cache";
        std::error_code ec;
        std::filesystem::create_directories(fake_home, ec);

        test_ext_soundness_detail::ScopedEnv env_home("HOME", fake_home.string());
#if defined(_WIN32)
        test_ext_soundness_detail::ScopedEnv env_up("USERPROFILE", fake_home.string());
        auto fake_local = fake_home / "AppData" / "Local";
        std::filesystem::create_directories(fake_local, ec);
        test_ext_soundness_detail::ScopedEnv env_la("LOCALAPPDATA", fake_local.string());
#endif
        {
            test_ext_soundness_detail::ScopedEnv unset_cf("CFIRE_CACHE_DIR", "", false);
            test_ext_soundness_detail::ScopedEnv unset_xdg("XDG_CACHE_HOME", "", false);
            auto r1 = cfire::build::user_cache_root_dir();
            auto r2 = cfire::detail::user_cache_dir("");
            require(r1 == r2, std::format("cache_paths and cfire_prelude match natively ({} == {})", r1.string(), r2.string()));
            require(std::filesystem::is_regular_file(r1 / ".cfire-cache", ec), "native user cache root carries .cfire-cache marker");
#if defined(__APPLE__)
            require(r1 == fake_home / "Library" / "Caches" / "cfire", "macOS cache root is ~/Library/Caches/cfire");
#elif defined(_WIN32)
            require(r1 == fake_local / "cfire" / "cache", "Windows cache root is %LOCALAPPDATA%\\cfire\\cache");
#else
            require(r1 == fake_home / ".cache" / "cfire", "Linux cache root is ~/.cache/cfire");
#endif

            test_ext_soundness_detail::ScopedEnv set_xdg("XDG_CACHE_HOME", fake_xdg.string());
            auto rx1 = cfire::build::user_cache_root_dir();
            auto rx2 = cfire::detail::user_cache_dir("");
            require(rx1 == fake_xdg / "cfire" && rx1 == rx2, "XDG_CACHE_HOME overrides OS-native cache root cleanly");
        }

        // Foreign directory without .cfire-cache: must never be stamped or deleted!
        auto foreign = cache_tmp.path / "foreign_dir";
        auto doc = foreign / "Documents" / "thesis.txt";
        test_ext_soundness_detail::write_file_checked(doc, "my thesis");
        {
            test_ext_soundness_detail::ScopedEnv set_cf("CFIRE_CACHE_DIR", foreign.string());
            auto proj_dir = cache_tmp.path / "tiny_proj";
            auto tiny = proj_dir / "main.cf";
            test_ext_soundness_detail::write_file_checked(tiny, "package main\nfn main() {}\n");
            (void)run_process({cfire, "check", tiny.string()}, proj_dir, 60);
            require(!std::filesystem::exists(foreign / ".cfire-cache", ec),
                "foreign CFIRE_CACHE_DIR must not be stamped with .cfire-cache");

            auto cl = run_process({cfire, "clean", "--cache"}, proj_dir, 60);
            require(cl.exit_code == 1 && (cl.out + cl.err).contains(".cfire-cache"),
                "cfire clean --cache exits 1 when non-empty CFIRE_CACHE_DIR lacks .cfire-cache");
            require(std::filesystem::is_regular_file(doc, ec),
                "foreign file under CFIRE_CACHE_DIR survives clean --cache");
        }

        // Refuse CFIRE_CACHE_DIR=$HOME and symlinked CFIRE_CACHE_DIR
        {
            test_ext_soundness_detail::ScopedEnv set_home_cache("CFIRE_CACHE_DIR", fake_home.string());
            auto cl_home = run_process({cfire, "clean", "--cache"}, cache_tmp.path, 60);
            require(cl_home.exit_code == 1, "cfire clean --cache refuses CFIRE_CACHE_DIR=$HOME");
            require(std::filesystem::is_directory(fake_home, ec), "$HOME survives refused clean --cache");
        }
#if !defined(_WIN32)
        {
            auto sym_cache = cache_tmp.path / "sym_cache";
            std::filesystem::create_directory_symlink(fake_override, sym_cache, ec);
            test_ext_soundness_detail::ScopedEnv set_sym("CFIRE_CACHE_DIR", sym_cache.string());
            auto cl_sym = run_process({cfire, "clean", "--cache"}, cache_tmp.path, 60);
            require(cl_sym.exit_code == 1 && std::filesystem::exists(std::filesystem::symlink_status(sym_cache, ec)),
                "cfire clean --cache refuses symlinked CFIRE_CACHE_DIR");
        }
#endif
    }

    // 5. Nested multi-driver `cfire clean`, OutDir ".", symlinks, --dry-run, --json, legacy sweep, read-only toolchain
    {
        ScopedTempDir clean_tmp("cfire_nested_clean");
        auto proj = clean_tmp.path / "proj";
        test_ext_soundness_detail::write_file_checked(proj / "build.cf",
            "package build\n"
            "import \"core:build\"\n"
            "Cli := build.Executable(\"mycli\", { Entry: \"cli/main.cf\", OutDir: \".\" })\n"
            "Gui := build.Executable(\"MyApp\", { Entry: \"src/drivers/gui/main.cf\", OutDir: \".\", Subsystem: \"gui\" })\n"
        );
        test_ext_soundness_detail::write_file_checked(proj / "cli" / "main.cf", "package main\nfn main() {}\n");
        test_ext_soundness_detail::write_file_checked(proj / "src" / "drivers" / "gui" / "main.cf", "package main\nfn main() {}\n");
        test_ext_soundness_detail::write_file_checked(proj / "cli" / ".cfire" / "obj" / "a.o", "obj");
        test_ext_soundness_detail::write_file_checked(proj / "src" / "drivers" / "gui" / ".cfire" / "gen" / "g.cpp", "cpp");
        test_ext_soundness_detail::write_file_checked(proj / "mycli", "bin");
        test_ext_soundness_detail::write_file_checked(proj / "MyApp.app" / "Contents" / "MacOS" / "MyApp", "app");

#if !defined(_WIN32)
        auto keep_dir = clean_tmp.path / "outside_target";
        auto keep_file = keep_dir / "keep.txt";
        test_ext_soundness_detail::write_file_checked(keep_file, "do-not-delete");
        std::error_code sec;
        std::filesystem::create_directories(proj / "sub", sec);
        std::filesystem::create_directory_symlink(keep_dir, proj / "sub" / ".cfire", sec);
#endif

        // --dry-run --json leaves everything intact
        auto dry = run_process({cfire, "clean", "--dry-run", "--json", proj.string()}, clean_tmp.path, 60);
        require(dry.exit_code == 0 && dry.out.contains("\"dry_run\": true"), "cfire clean --dry-run --json exits 0");
        std::error_code ec;
        require(std::filesystem::exists(proj / "cli" / ".cfire" / "obj" / "a.o", ec), "dry-run preserves artifacts");
        require(std::filesystem::exists(proj / "mycli", ec), "dry-run preserves target binary");

        // Real clean removes nested .cfire, target outputs, and the .cfire symlink itself (not its target!)
        auto res = run_process({cfire, "clean", "--json", proj.string()}, clean_tmp.path, 60);
        require(res.exit_code == 0 && res.out.contains("\"ok\": true"),
            std::format("nested multi-driver cfire clean succeeds (out={})", res.out));
        require(!std::filesystem::exists(proj / "cli" / ".cfire", ec), "nested cli/.cfire removed");
        require(!std::filesystem::exists(proj / "src" / "drivers" / "gui" / ".cfire", ec), "nested gui/.cfire removed");
        require(!std::filesystem::exists(proj / "mycli", ec), "exact OutDir='.' native binary removed");
        require(!std::filesystem::exists(proj / "MyApp.app", ec), "exact OutDir='.' .app bundle removed");
        require(std::filesystem::exists(proj / "cli" / "main.cf", ec), "OutDir='.' preserves .cf source files");
        require(std::filesystem::exists(proj / "build.cf", ec), "OutDir='.' preserves build.cf");
#if !defined(_WIN32)
        require(!std::filesystem::exists(std::filesystem::symlink_status(proj / "sub" / ".cfire", ec)),
            "symlinked .cfire link removed");
        require(std::filesystem::is_regular_file(keep_file, ec),
            "symlinked .cfire target contents are never followed or deleted");
#endif

        // Tainted .cfire/ holding unexpected files must trigger safety guard & exit 1
        test_ext_soundness_detail::write_file_checked(proj / "sub" / ".cfire" / "custom_notes.txt", "keep me");
        auto tainted = run_process({cfire, "clean", proj.string()}, clean_tmp.path, 60);
        require(tainted.exit_code == 1 && std::filesystem::exists(proj / "sub" / ".cfire" / "custom_notes.txt", ec),
            "cfire clean skips tainted .cfire/ with exit 1");

        // Legacy ~/.cfire/cache sweep preserves ~/.cfire/{config.json,bin,runtime}
        {
            auto leg_home = clean_tmp.path / "leg_home";
            auto leg_xdg = clean_tmp.path / "leg_xdg";
            test_ext_soundness_detail::write_file_checked(leg_home / ".cfire" / "cache" / "pch" / "old.pch", "pch");
            test_ext_soundness_detail::write_file_checked(leg_home / ".cfire" / "config.json", "{}\n");
            test_ext_soundness_detail::write_file_checked(leg_home / ".cfire" / "bin" / "cfire", "bin");
            test_ext_soundness_detail::write_file_checked(leg_home / ".cfire" / "runtime" / "cfire_prelude.hpp", "// rt\n");
            test_ext_soundness_detail::ScopedEnv e_home("HOME", leg_home.string());
#if defined(_WIN32)
            test_ext_soundness_detail::ScopedEnv e_up("USERPROFILE", leg_home.string());
#endif
            test_ext_soundness_detail::ScopedEnv e_xdg("XDG_CACHE_HOME", leg_xdg.string());
            test_ext_soundness_detail::ScopedEnv e_cf("CFIRE_CACHE_DIR", "", false);
            test_ext_soundness_detail::ScopedEnv e_sw("CFIRE_CLEAN_SWEEP_LEGACY", "1");
            auto lres = run_process({cfire, "clean", "--cache"}, clean_tmp.path, 60);
            require(lres.exit_code == 0, std::format("legacy sweep clean --cache exits 0 (err={})", lres.err));
            require(!std::filesystem::exists(leg_home / ".cfire" / "cache", ec), "legacy ~/.cfire/cache swept");
            require(std::filesystem::exists(leg_home / ".cfire" / "config.json", ec) &&
                    std::filesystem::exists(leg_home / ".cfire" / "bin" / "cfire", ec) &&
                    std::filesystem::exists(leg_home / ".cfire" / "runtime" / "cfire_prelude.hpp", ec),
                "~/.cfire/{config.json,bin,runtime} preserved by legacy sweep");
        }

#if !defined(_WIN32)
        // Read-only toolchain directory must receive zero writes from check/inspect
        {
            auto ro_tc = clean_tmp.path / "ro_toolchain";
            auto ro_rt = ro_tc / "runtime";
            std::filesystem::create_directories(ro_rt, ec);
            (void)cfire::fs::write_text(ro_rt / "cfire_prelude.hpp", "// ro\n");
            ::chmod(ro_rt.c_str(), 0555);
            ::chmod(ro_tc.c_str(), 0555);
            auto ws_chk = clean_tmp.path / "ro_ws";
            test_ext_soundness_detail::write_file_checked(ws_chk / "m.cf", "package main\nfn main() {}\n");
            test_ext_soundness_detail::ScopedEnv e_rt("CFIRE_RUNTIME_DIR", ro_rt.string());
            auto insp_ro = run_process({cfire, "inspect", "cpp stdio.h"}, ws_chk, 60);
            (void)insp_ro;
            require(!std::filesystem::exists(ro_tc / ".cfire", ec),
                "inspect against read-only toolchain writes nothing into toolchain dir");
            ::chmod(ro_tc.c_str(), 0755);
            ::chmod(ro_rt.c_str(), 0755);
        }
#endif
    }

    // 6. MCP parity (CF-239): workspace build.cf header flags + workspace cache + StdTypeDoc & BuiltinMethod lookup
    {
        ScopedTempDir mcp_ws("cfire_mcp_parity");
        test_ext_soundness_detail::write_file_checked(mcp_ws.path / "build.cf",
            "package build\n"
            "import \"core:build\"\n"
            "Pkg := build.Package({ Name: \"mcp_probe\", Includes: [\"inc\"], Defines: [\"ENABLE_MCP_PROBE=1\"] })\n"
        );
        test_ext_soundness_detail::write_file_checked(mcp_ws.path / "inc" / "probe_hdr.h",
            "#pragma once\n"
            "#ifdef ENABLE_MCP_PROBE\n"
            "int cfire_mcp_probe_fn(int a);\n"
            "#endif\n"
        );
        std::error_code ec;
        auto dummy_rt = mcp_ws.path / "tc" / "runtime";
        std::filesystem::create_directories(dummy_rt, ec);
        auto srv = cfire::mcp::build_compiler_mcp_server(dummy_rt, cfire::mcp::McpOptions{mcp_ws.path, false});
        auto call_tool = [&](std::string_view name, std::string_view args_json) {
            return srv.handle_jsonrpc(std::format(
                R"({{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{{"name":"{}","arguments":{}}}}})",
                name, args_json));
        };

        auto hdr_resp = call_tool("cfire_inspect", R"({"target":"cpp probe_hdr.h"})");
        require(hdr_resp.contains("cfire_mcp_probe_fn"),
            std::format("MCP cfire_inspect(cpp) honors workspace build.cf Includes/Defines (got: {})", hdr_resp));
        require(std::filesystem::exists(mcp_ws.path / ".cfire" / "cache" / "ast", ec),
            "MCP cfire_inspect(cpp) writes AST cache under <workspace>/.cfire/cache");
        require(!std::filesystem::exists(dummy_rt.parent_path() / ".cfire", ec),
            "MCP cfire_inspect(cpp) writes nothing into toolchain/.cfire");

        auto alias_lookup = call_tool("cfire_stdlib_lookup", R"({"query":"ext:ssh"})");
        require(alias_lookup.contains("cfire/INVALID-IMPORT") && alias_lookup.contains("ext:net/ssh") &&
                alias_lookup.contains("SftpEntry"),
            "MCP cfire_stdlib_lookup notes INVALID-IMPORT on ext:ssh and shows StdTypeDoc SftpEntry");

        auto bm_lookup = call_tool("cfire_stdlib_lookup", R"({"query":"str.split_n"})");
        require(bm_lookup.contains("builtin") && bm_lookup.contains("split_n"),
            "MCP cfire_stdlib_lookup includes builtin_methods entries");

        auto type_insp = call_tool("cfire_inspect", R"({"target":"http.Request"})");
        require(type_insp.contains("body: str") && type_insp.contains("headers: map[str]str"),
            "MCP cfire_inspect resolves StdTypeDoc struct fields for http.Request");

        auto var_insp = call_tool("cfire_inspect", R"({"target":"log.Level::Debug"})");
        require(var_insp.contains("log.Level::Debug") && !var_insp.contains("uint8_t"),
            "MCP cfire_inspect resolves enum variants in pure cfire terms");

        auto bm_insp = call_tool("cfire_inspect", R"({"target":"slice.binary_search"})");
        require(bm_insp.contains("binary_search") && bm_insp.contains("usize?"),
            "MCP cfire_inspect resolves builtin_methods (slice.binary_search)");
    }

    std::println("--- PASS: test_ext_soundness_cache_and_clean");
}
