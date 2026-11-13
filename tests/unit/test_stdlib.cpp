#include "../test_harness.hpp"
#include "mcp.hpp"

void test_crypto_sha256_and_csprng([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_crypto_sha256_and_csprng");
    // Standard NIST FIPS 180-4 SHA-256 test vectors:
    auto h_empty = cfire::sha256::hash_hex("");
    require(h_empty == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "sha256 empty string test vector");

    auto h_abc = cfire::sha256::hash_hex("abc");
    require(h_abc == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "sha256 'abc' NIST test vector");

    auto b_abc = cfire::sha256::hash_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>("abc"), 3));
    require(b_abc.size() == 32 && static_cast<unsigned char>(b_abc[0]) == 0xba,
        "sha256 hash_bytes vector");

    // blake3 compatibility alias
    require(cfire::blake3::hash_hex("abc") == h_abc,
        "blake3 backward-compat alias matching sha256");

    // RFC 4231 HMAC-SHA256 test vector
    auto hmac_res = cfire::hmac::sha256_hex("key", "The quick brown fox jumps over the lazy dog");
    require(hmac_res == "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8",
        "RFC 4231 HMAC-SHA256 test vector");

    // CSPRNG bytes from OS entropy
    auto rand_b = cfire::rand::bytes(32);
    require(rand_b.size() == 32, "rand.bytes length check");
    bool all_zero = true;
    for (auto b : rand_b) {
        if (static_cast<unsigned char>(b) != 0) { all_zero = false; break; }
    }
    require(!all_zero, "rand.bytes entropy check");
    std::println("--- PASS: test_crypto_sha256_and_csprng");
}

void test_composable_expression_try([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_composable_expression_try");
    std::string_view src = R"(package main

fn fail_or_num(fail: bool) -> i32! {
if fail {
    return error("failure triggered")
}
return 25
}

fn compute(fail: bool) -> i32! {
val := 100 + try fail_or_num(fail)
return val
}

fn main() {
ok_val := try compute(false)
assert(ok_val == 125, "expression try success unwrapping")
}
)";
    auto ast = cfire::frontend::parse_source("test_expr_try.cf", src);
    require(ast.has_value(), ast.has_value() ? "parse test_expr_try.cf" : ast.error());
    if (!ast.has_value()) return;
    auto checked = cfire::frontend::analyze(std::move(*ast));
    require(checked.has_value(), checked.has_value() ? "analyze test_expr_try.cf" : checked.error());
    if (!checked.has_value()) return;
    std::string cpp = cfire::frontend::emit_cpp(*checked);
    require(cpp.contains("cfire::unwrap_val"), "codegen contains statement-expression cfire::unwrap_val");
    require(cpp.contains("std::unexpected"), "codegen contains early return std::unexpected");

    // Compile and execute the generated C++ to verify end-to-end correctness
    auto res = compile_and_run_cpp(cpp, rt_dir, "cfire_expr_try");
    res.assert_ok("expr-try C++ execution");
    std::println("--- PASS: test_composable_expression_try");
}

void test_buildsys_incremental_caching([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_buildsys_incremental_caching");
    ScopedTempDir test_dir("cfire_bs02");
    auto c_file = test_dir.subpath("src") / "fast_calc.c";
    std::ofstream(c_file) << "int fast_add(int a, int b) { return a + b; }\n";

    cfire::build::BuildPlan plan;
    cfire::build::ExternalDepSpec dep;
    dep.name = "fastcalc";
    dep.driver = "CC";
    dep.source = test_dir.path.string();

    auto res1 = cfire::build::build_local_dep(test_dir.path, dep, plan);
    require(res1.has_value(), "first build_local_dep");
    require(std::filesystem::exists(dep.built_archive), "built archive exists");

    // Verify object file stem was disambiguated with relative path hash
    auto obj_dir = test_dir.path / ".cfire" / "cache" / "objs" / "fastcalc";
    bool found_disambiguated_obj = false;
    for (const auto& entry : std::filesystem::directory_iterator(obj_dir)) {
        if (entry.path().extension() == ".o" && entry.path().stem().string().starts_with("fast_calc_")) {
            found_disambiguated_obj = true;
            break;
        }
    }
    require(found_disambiguated_obj, "object file disambiguated with relative path hash");

    // Record archive write time
    auto ar_time1 = std::filesystem::last_write_time(dep.built_archive);

    // Run second build immediately without modifying source
    cfire::build::BuildPlan plan2;
    auto res2 = cfire::build::build_local_dep(test_dir.path, dep, plan2);
    require(res2.has_value(), "second build_local_dep");
    auto ar_time2 = std::filesystem::last_write_time(dep.built_archive);
    require(ar_time1 == ar_time2, "unmodified build.CC dependency skips re-compilation and re-archiving");

    // Test parsing build.Executable in build.cf
    auto build_cf_path = test_dir.path / "build.cf";
    {
        std::ofstream bcf(build_cf_path);
        bcf << R"(package build

import "core:build"

DemoApp := build.Executable("demo-desktop", {
    Entry: "src/main.cf",
    Subsystem: "gui",
    Name: "Demo App",
    BundleId: "com.example.demo",
    Icon: "assets/icon.png",
    Targets: ["native", "wasm"],
})
)";
    }
    auto parsed_bcf = cfire::build::parse_build_cf(build_cf_path);
    require(parsed_bcf.has_value(), "parse_build_cf succeeded");
    require(parsed_bcf->executables.size() == 1, "found 1 executable");
    const auto& exe = parsed_bcf->executables[0];
    require(exe.name == "demo-desktop", "executable name matches");
    require(exe.entry == "src/main.cf", "executable entry matches");
    require(exe.subsystem == "gui", "executable subsystem matches");
    require(exe.app_name == "Demo App", "executable app_name matches");
    require(exe.bundle_id == "com.example.demo", "executable bundle_id matches");
    require(exe.icon == "assets/icon.png", "executable icon matches");
    require(exe.targets.size() == 2 && exe.targets[0] == "native" && exe.targets[1] == "wasm", "executable targets match");

    auto res_target = cfire::build::resolve_build_target(test_dir.path, "", true);
    require(res_target.has_value(), "resolve_build_target succeeded");
    require(res_target->has_wasm_target, "resolve_build_target identified wasm target");
    require(res_target->has_native_target, "resolve_build_target identified native target");
    require(res_target->wasm_html.filename() == "demo-desktop.html", "resolve_build_target wasm html filename matches");
    require(res_target->exe.name == "demo-desktop", "resolve_build_target exe matches");

    std::println("--- PASS: test_buildsys_incremental_caching");
}

void test_error_handling_divergence([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_error_handling_divergence");
    require_sema_error(R"(package main
fn fallible() -> i32! { return 42 }
fn main() {
val := fallible() or err {
    println("logging error: {}", err)
}
println("val is: {}", val)
}
)", "cfire/NON-DIVERGING-OR-BLOCK", "test_bad_or_diverge.cf");

    require_sema_ok(R"(package main
fn fallible() -> i32! { return 42 }
fn run() -> i32 {
val := fallible() or err { return -1 }
return val
}
fn main() {
res := run()
assert(res == 42, "diverging or block valid")
}
)", "test_good_or_diverge.cf");
    std::println("--- PASS: test_error_handling_divergence");
}

void test_subprocess_spawning_and_capture([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_subprocess_spawning_and_capture");
    // Arguments with spaces must be preserved without shell escaping issues
    auto res = cfire::os::command("echo", {"hello world", "arg with spaces"});
    require(res.has_value(), "cfire::os::command echo execution");
    if (res.has_value()) {
        require(res->exit_code == 0, "echo exit code 0");
        require(res->stdout_cf_.contains("hello world arg with spaces"),
            "stdout contains literal argument contents without shell splitting");
    }

    // Spawning nonexistent executable returns error
    auto bad_res = cfire::os::command("/bin/nonexistent_command_xyz", {"foo"});
    require(!bad_res.has_value(), "nonexistent command returns unexpected");
    std::println("--- PASS: test_subprocess_spawning_and_capture");
}

void test_cpp_ast_ingestion_and_overloads([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cpp_ast_ingestion_and_overloads");
    std::string_view ast_json = R"json({
  "kind": "TranslationUnitDecl",
  "inner": [
{
  "kind": "NamespaceDecl",
  "name": "Math",
  "inner": [
    {
      "kind": "FunctionDecl",
      "name": "add",
      "type": { "qualType": "int (int, int)" }
    },
    {
      "kind": "FunctionDecl",
      "name": "add",
      "type": { "qualType": "double (double, double)" }
    }
  ]
}
  ]
})json";
    std::unordered_map<std::string, cfire::bridge::CppSymbol> syms;
    cfire::bridge::extract_clang_json_symbols(ast_json, syms);

    require(syms.contains("Math::add"), "ClangAstParser extracts namespaced symbol Math::add");
    if (syms.contains("Math::add")) {
        const auto& sym = syms["Math::add"];
        require(sym.name == "add", "symbol name is add");
        require(sym.return_type == "int", "primary overload return type is int");
        require(sym.param_types.size() == 2, "primary overload has 2 params");
        require(sym.overloads.size() == 1, "Math::add captured secondary overload");
        if (!sym.overloads.empty()) {
            require(sym.overloads[0].return_type == "double", "secondary overload return type is double");
        }
    }

    // Verify cache serialization and deserialization preserves overloads
    cfire::bridge::HeaderIndex idx;
    idx.header = "<test_math.h>";
    idx.symbols = syms;
    ScopedTempDir cache_tmp_scope("cfire_cache_tmp");  // hermetic, mkdtemp-backed (CF-153)
    auto cache_tmp = cache_tmp_scope.path / "cfire_bs03_test.idx";
    cfire::bridge::save_cached_index(cache_tmp, idx);

    auto loaded = cfire::bridge::load_cached_index(cache_tmp, "<test_math.h>");
    require(loaded.has_value(), "load_cached_index loaded saved index");
    if (loaded.has_value()) {
        require(loaded->symbols.contains("Math::add"), "loaded cache contains Math::add");
        if (loaded->symbols.contains("Math::add")) {
            const auto& loaded_sym = loaded->symbols.at("Math::add");
            require(loaded_sym.overloads.size() == 1, "loaded cache preserves overloads");
        }
    }
    std::filesystem::remove(cache_tmp);
    std::println("--- PASS: test_cpp_ast_ingestion_and_overloads");
}

void test_parameter_intent_validation([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_parameter_intent_validation");
    std::string_view bad_src = R"(package main
fn compute(forward x: i32) -> i32 {
return x + 1
}
fn main() {}
)";
    auto bad_ast = cfire::frontend::parse_source("test_bad_forward.cf", bad_src);
    require(!bad_ast.has_value(), "expected parser to reject 'forward' parameter intent");
    std::println("--- PASS: test_parameter_intent_validation");
}

void test_cpp_escape_hatch_execution([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cpp_escape_hatch_execution");
    std::string_view src = R"(package main
fn main() {
cpp {
    int val = 42;
    std::println("cpp escape hatch: {}", val);
}
}
)";
    auto ast = cfire::frontend::parse_source("test_cpp_escape.cf", src);
    require(ast.has_value(), "parse test_cpp_escape.cf with cpp { ... }");
    if (ast.has_value()) {
        auto checked = cfire::frontend::analyze(std::move(*ast));
        require(checked.has_value(), "analyze test_cpp_escape.cf");
        if (checked.has_value()) {
            std::string cpp_code = cfire::frontend::emit_cpp(*checked);
            require(cpp_code.contains("int val = 42;"), "codegen preserves cpp block verbatim");
        }
    }
    std::println("--- PASS: test_cpp_escape_hatch_execution");
}

void test_http_client_server_and_urls([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_http_client_server_and_urls");
    // A. URL parsing
    auto u_res = cfire::url::parse("http://127.0.0.1:8080/api/items?category=shoes&limit=10#details");
    require(u_res.has_value(), "url.parse valid RFC 3986 URL");
    if (u_res.has_value()) {
        require(u_res->scheme == "http", "url scheme is http");
        require(u_res->host == "127.0.0.1", "url host is 127.0.0.1");
        require(u_res->port == 8080, "url port is 8080");
        require(u_res->path == "/api/items", "url path is /api/items");
        require(u_res->query == "category=shoes&limit=10", "url query parsed");
        require(u_res->fragment == "details", "url fragment is details");
        require(u_res->query_params.size() == 2, "query_params has 2 entries");
        require(u_res->query_params["category"] == "shoes", "query_param category is shoes");
        require(u_res->query_params["limit"] == "10", "query_param limit is 10");
    }

    // B. URL encode / decode
    std::string original = "hello world & foo=bar/baz+100%";
    std::string enc = cfire::url::encode(original);
    require(!enc.contains(" "), "encoded URL does not contain spaces");
    std::string dec = cfire::url::decode(enc);
    require(dec == original, "url.decode roundtrip matches original");

    // C. HTTP Server and Client roundtrip over loopback
    auto srv_res = cfire::http::make_server(0);
    require(srv_res.has_value(), "cfire::http::make_server creation");
    if (srv_res.has_value()) {
        auto port = srv_res->port();
        std::thread srv_th([&]() {
            srv_res->serve_once([](const cfire::http::Request& req) -> cfire::http::Response {
                cfire::http::Response resp;
                resp.status_code = 200;
                resp.status_text = "OK";
                resp.headers["content-type"] = "application/json";
                resp.body = std::format("{{\"method\":\"{}\",\"path\":\"{}\"}}", req.method, req.path);
                return resp;
            });
        });

        std::string target_url = std::format("http://127.0.0.1:{}/api/test", port);
        auto resp_res = cfire::http::get(target_url);
        require(resp_res.has_value(), "cfire::http::get loopback request");
        if (resp_res.has_value()) {
            require(resp_res->status_code == 200, "http response status code 200");
            require(resp_res->is_ok(), "http response is_ok() true");
            require(resp_res->body.contains("\"method\":\"GET\""), "response body reflects GET method");
            require(resp_res->body.contains("\"path\":\"/api/test\""), "response body reflects path");
        }
        if (srv_th.joinable()) {
            srv_th.join();
        }
    }
    std::println("--- PASS: test_http_client_server_and_urls");
}

void test_monadic_type_orthogonality([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_monadic_type_orthogonality");
    std::string_view src = R"(package main

type Item struct {
Name: str?
Score: i32!
Tags: []str?
}

fn main() {
println("monadic types struct check")
}
)";
    auto ast = cfire::frontend::parse_source("test_monadic_types.cf", src);
    require(ast.has_value(), "parse test_monadic_types.cf");
    if (ast.has_value()) {
        auto checked = cfire::frontend::analyze(std::move(*ast));
        require(checked.has_value(), "analyze test_monadic_types.cf");
        if (checked.has_value()) {
            std::string cpp = cfire::frontend::emit_cpp(*checked);
            require(cpp.contains("std::optional<std::string> Name"),
                "struct field 'Name: str?' lowers to std::optional<std::string>");
            require(cpp.contains("std::expected<std::int32_t, cfire::Error> Score"),
                "struct field 'Score: i32!' lowers to std::expected<std::int32_t, cfire::Error>");
            require(cpp.contains("std::vector<std::optional<std::string>> Tags"),
                "struct field 'Tags: []str?' lowers to std::vector<std::optional<std::string>>");

            // End-to-end compile with clang++ to verify C++23 validity
            auto res = compile_and_run_cpp(cpp, rt_dir, "cfire_monadic");
            res.assert_ok("monadic C++ execution");
        }
    }
    std::println("--- PASS: test_monadic_type_orthogonality");
}

void test_structured_concurrency_lifecycle([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_structured_concurrency_lifecycle");
    std::string_view src = R"(package main

fn set_val(v: *mut i32, n: i32) {
*v = n
}

fn test_scope_defer() -> i32 {
mut vals: []i32 = [10]
ptr := &mut vals[0]
async.scope(workers: 2) |scope| {
    defer set_val(ptr, 99)
    for i in 0..1 {
        slot := &mut vals[i]
        scope.spawn || {
            *slot = 42
        }
    }
}
return vals[0]
}

fn main() {
v := test_scope_defer()
println("final val: {}", v)
}
)";
    auto ast = cfire::frontend::parse_source("test_scope_raii.cf", src);
    require(ast.has_value(), "parse test_scope_raii.cf");
    if (ast.has_value()) {
        auto checked = cfire::frontend::analyze(std::move(*ast));
        require(checked.has_value(), "analyze test_scope_raii.cf");
        if (checked.has_value()) {
            std::string cpp = cfire::frontend::emit_cpp(*checked);
            require(cpp.contains("cfire::async::Scope scope(2);"),
                "async.scope lowers to RAII Scope declaration");
            require(cpp.contains("scope.defer([&] {"),
                "defer inside async.scope routes to scope.defer(...)");
            require(cpp.contains("scope.join_all();"),
                "async.scope block explicitly joins on exit");

            auto res = compile_and_run_cpp(cpp, rt_dir, "cfire_scope");
            res.assert_ok("async.scope binary execution");
            res.assert_contains("final val: 99", "defer in async.scope executed strictly after worker thread task completed");
        }
    }
    std::println("--- PASS: test_structured_concurrency_lifecycle");
}

void test_explicit_borrow_references([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_explicit_borrow_references");
    std::string_view src = R"(package main

fn inspect_borrows(borrow_str: &str, owned_str: str, borrow_slice: &[]i32, owned_slice: []i32, mut_slice: &mut []i32) {
}

fn main() {
}
)";
    auto ast = cfire::frontend::parse_source("test_borrows.cf", src);
    require(ast.has_value(), "parse test_borrows.cf");
    if (ast.has_value()) {
        auto checked = cfire::frontend::analyze(std::move(*ast));
        require(checked.has_value(), "analyze test_borrows.cf");
        if (checked.has_value()) {
            std::string cpp = cfire::frontend::emit_cpp(*checked);
            require(cpp.contains("cfire::str_ref borrow_str"),
                "&str parameter lowers to cfire::str_ref");
            require(cpp.contains("cfire::str_ref owned_str"),
                "read-only str parameter lowers to the cfire::str_ref view (Q36)");
            require(cpp.contains("std::span<const std::int32_t> borrow_slice"),
                "&[]i32 parameter lowers to std::span<const std::int32_t>");
            require(cpp.contains("const std::vector<std::int32_t>& owned_slice"),
                "owned []i32 parameter lowers to const std::vector<std::int32_t>&");
            require(cpp.contains("std::span<std::int32_t> mut_slice"),
                "&mut []i32 parameter lowers to std::span<std::int32_t>");
        }
    }
    std::println("--- PASS: test_explicit_borrow_references");
}

void test_cfg_move_analysis([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cfg_move_analysis");
    // 1. Re-assignment after move is allowed
    std::string_view src_reassign = R"(package main

fn consume(move s: str) {}

fn test_reassign() {
mut s := "hello"
consume(move s)
s = "world"
consume(move s)
}
)";
    auto ast1 = cfire::frontend::parse_source("test_reassign.cf", src_reassign);
    require(ast1.has_value(), "parse test_reassign.cf");
    if (ast1.has_value()) {
        auto checked = cfire::frontend::analyze(std::move(*ast1));
        require(checked.has_value(), "reassignment after move is permitted");
    }

    // 2. Move in loop body without reassignment is rejected
    std::string_view src_loop_move = R"(package main

fn consume(move s: str) {}

fn test_loop_move() {
s := "loop_item"
for i in [1, 2, 3] {
    consume(move s)
}
}
)";
    auto ast2 = cfire::frontend::parse_source("test_loop_move.cf", src_loop_move);
    require(ast2.has_value(), "parse test_loop_move.cf");
    if (ast2.has_value()) {
        auto checked = cfire::frontend::analyze(std::move(*ast2));
        require(!checked.has_value(), "move in loop body without reassignment must fail");
        if (!checked.has_value()) {
            require(checked.error().contains("cfire/USE-AFTER-MOVE"),
                "loop move error reports cfire/USE-AFTER-MOVE");
        }
    }

    // 3. Move in loop body WITH reassignment is permitted
    std::string_view src_loop_reassign = R"(package main

fn consume(move s: str) {}

fn test_loop_reassign() {
mut s := "initial"
for i in [1, 2, 3] {
    consume(move s)
    s = "reset"
}
}
)";
    auto ast3 = cfire::frontend::parse_source("test_loop_reassign.cf", src_loop_reassign);
    require(ast3.has_value(), "parse test_loop_reassign.cf");
    if (ast3.has_value()) {
        auto checked = cfire::frontend::analyze(std::move(*ast3));
        require(checked.has_value(), "move in loop body with reassignment is permitted");
    }

    // 4. Branch move: move in only one if branch does NOT mark variable moved after if
    std::string_view src_branch_single = R"(package main

fn consume(move s: str) {}

fn test_branch(cond: bool) {
s := "branch"
if cond {
    consume(move s)
}
// s is not definitely moved because else path was not taken
}
)";
    auto ast4 = cfire::frontend::parse_source("test_branch.cf", src_branch_single);
    require(ast4.has_value(), "parse test_branch.cf");
    if (ast4.has_value()) {
        auto checked = cfire::frontend::analyze(std::move(*ast4));
        require(checked.has_value(), "move in only one if branch does not mark variable moved after if");
    }

    // 5. Branch move: move in BOTH branches marks variable moved after if
    std::string_view src_branch_both = R"(package main

fn consume(move s: str) {}

fn test_branch_both(cond: bool) {
s := "branch"
if cond {
    consume(move s)
} else {
    consume(move s)
}
consume(move s) // Error: definitely moved along both branches
}
)";
    auto ast5 = cfire::frontend::parse_source("test_branch_both.cf", src_branch_both);
    require(ast5.has_value(), "parse test_branch_both.cf");
    if (ast5.has_value()) {
        auto checked = cfire::frontend::analyze(std::move(*ast5));
        require(!checked.has_value(), "use after move when moved along both branches must fail");
        if (!checked.has_value()) {
            require(checked.error().contains("cfire/USE-AFTER-MOVE"),
                "move along all divergent branches triggers cfire/USE-AFTER-MOVE");
        }
    }
    std::println("--- PASS: test_cfg_move_analysis");
}

void test_data_encodings_base64_hex_csv([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_data_encodings_base64_hex_csv");
    // 1. Base64
    std::string plain = "hello, cfire!";
    std::string b64 = cfire::base64::encode_str(plain);
    auto dec = cfire::base64::decode_str(b64);
    require(dec.has_value() && *dec == plain, "base64 encode/decode roundtrip");

    // Base64 URL-safe test
    std::string url_plain = "https://example.com/api?q=hello&id=123";
    std::string url_b64 = cfire::base64::encode_str(url_plain, true);
    require(!url_b64.contains('+') && !url_b64.contains('/'), "url_safe base64 contains no + or /");
    auto url_dec = cfire::base64::decode_str(url_b64);
    require(url_dec.has_value() && *url_dec == url_plain, "url_safe base64 decode roundtrip");

    // 2. Hex
    std::string hex_str = cfire::hex::encode_str("cfire");
    require(hex_str == "6366697265", "hex encoding of 'cfire'");
    auto hex_dec = cfire::hex::decode_str(hex_str);
    require(hex_dec.has_value() && *hex_dec == "cfire", "hex decoding roundtrip");

    // 3. CSV RFC 4180 parsing and stringification
    std::string raw_csv = "id,name,notes\n1,\"Alice, Smith\",\"Multi-line\nNote\"\n2,Bob,\"Special \"\"Quotes\"\"\"";
    auto table = cfire::csv::parse(raw_csv);
    require(table.size() == 3, "csv parsed 3 rows");
    require(table[1][1] == "Alice, Smith", "csv parsed comma within quotes");
    require(table[1][2] == "Multi-line\nNote", "csv parsed embedded newline");
    require(table[2][2] == "Special \"Quotes\"", "csv parsed escaped double quotes");

    std::string serialized = cfire::csv::stringify(table);
    auto table2 = cfire::csv::parse(serialized);
    require(table2 == table, "csv stringify and re-parse roundtrip");

    // 4. Substring Search Primitives in core:str (STDLIB-P1-04)
    std::string_view sample_path = "/usr/local/include/sqlite/sqlite.h";
    require(cfire::str::index_of(sample_path, "/") == 0, "index_of first slash");
    require(cfire::str::index_of(sample_path, "local") == 5, "index_of 'local'");
    require(cfire::str::index_of(sample_path, "nonexistent") == -1, "index_of not found returns -1");
    require(cfire::str::index_of(sample_path, "sqlite", 20) == 26, "index_of with start_pos offset");
    require(cfire::str::last_index_of(sample_path, "/") == 25, "last_index_of slash");
    require(cfire::str::last_index_of(sample_path, ".h") == 32, "last_index_of '.h'");
    require(cfire::str::last_index_of(sample_path, "missing") == -1, "last_index_of not found returns -1");
    require(cfire::str::rfind(sample_path, "/") == 25, "rfind default start_pos");
    require(cfire::str::rfind(sample_path, "/", 24) == 18, "rfind with backward start_pos constraint");

    // 5. Numeric Prelude Conversion Constructors (LANG-P0-01)
    double pi_dbl = 3.141592653589793;
    float pi_flt = 3.14159f;
    require(cfire::i32(pi_dbl) == 3, "i32(double)");
    require(cfire::i64(pi_dbl) == 3, "i64(double)");
    require(cfire::u32(100.9) == 100, "u32(double)");
    require(cfire::u64(pi_flt) == 3, "u64(float)");
    require(cfire::f32(42) == 42.0f, "f32(int)");
    require(cfire::f64(42) == 42.0, "f64(int)");
    // Q9 (CF-165): T(x) panics when the value doesn't fit; wrapping is explicit.
    require(cfire::wrapping_as<std::int8_t>(300) == 44, "i8 overflow wrapping via wrapping_as");
    require(cfire::wrapping_as<std::uint8_t>(-1) == 255, "u8 underflow wrapping via wrapping_as");
    require(cfire::saturating_as<std::uint8_t>(-1) == 0, "u8 saturating_as clamps at 0");
    require(!cfire::try_as<std::uint8_t>(300).has_value(), "u8 try_as rejects out-of-range values");
    require(cfire::byte(0xFE) == 0xFE, "byte(int)");
    require(cfire::usize(12345) == 12345, "usize(int)");
    require(cfire::isize(-54321) == -54321, "isize(int)");

    // 6. Bitwise Operator Overloads on Imported C / Vendor Enums (LANG-P1-03)
    enum TestBitmask {
        MASK_NONE = 0,
        MASK_READ = 1,
        MASK_WRITE = 2,
        MASK_EXEC = 4
    };
    TestBitmask perms = MASK_READ | MASK_WRITE;
    require((perms & MASK_READ) == MASK_READ, "enum bitwise & check");
    require((perms & MASK_WRITE) == MASK_WRITE, "enum bitwise & check 2");
    require((perms & MASK_EXEC) == 0, "enum bitwise & check missing");
    perms |= MASK_EXEC;
    require((perms & MASK_EXEC) == MASK_EXEC, "enum bitwise |= check");
    perms &= ~MASK_WRITE;
    require((perms & MASK_WRITE) == 0, "enum bitwise &= with ~ check");
    require((perms ^ MASK_READ) == MASK_EXEC, "enum bitwise ^ check");
    // Test mixed enum & integral operations
    TestBitmask mixed = MASK_READ | 4;
    require((mixed & MASK_EXEC) == MASK_EXEC, "enum mixed bitwise | check");
    mixed &= ~1;
    require((mixed & MASK_READ) == 0, "enum mixed bitwise &= check");
    // Test std::formatter for enums
    std::string fmt_str = std::format("perms={}", perms);
    require(fmt_str == "perms=5", "enum std::formatter specialization check");

    // 7. String & Slice Pointer Offsets and C FFI Interop (LANG-P1-02)
    std::string msg = "Hello, Cfire!";
    const char* p_start = cfire::str::byte_ptr(msg);
    require(p_start != nullptr && *p_start == 'H', "byte_ptr on std::string");
    char* p_mut = cfire::str::byte_ptr(msg);
    *p_mut = 'h';
    require(msg[0] == 'h', "byte_ptr mutable char* mutation");
    msg[0] = 'H'; // revert

    const char* p_off = cfire::str::byte_ptr_at(msg, 7);
    require(std::string_view(p_off) == "Cfire!", "byte_ptr_at valid offset");
    const char* p_neg = cfire::str::byte_ptr_at(msg, -5);
    require(p_neg == msg.data(), "byte_ptr_at negative offset clamps to 0");
    const char* p_past = cfire::str::byte_ptr_at(msg, 100);
    require(p_past == msg.data() + msg.size(), "byte_ptr_at past end clamps to end");

    std::vector<int> nums = {10, 20, 30, 40};
    int* p_num = cfire::str::byte_ptr(nums);
    require(p_num != nullptr && *p_num == 10, "byte_ptr on std::vector");
    int* p_num_off = cfire::str::byte_ptr_at(nums, 2);
    require(p_num_off != nullptr && *p_num_off == 30, "byte_ptr_at on std::vector");

    // Slice / Vector .data() / .data_ptr() C FFI buffer pointer access (LANG-P1-06)
    int* p_vec_data = nums.data();
    require(p_vec_data != nullptr && *p_vec_data == 10, "nums.data() returns valid element pointer");
    std::span<int> nums_span = nums;
    int* p_span_data = nums_span.data();
    require(p_span_data != nullptr && *p_span_data == 10, "nums_span.data() returns valid element pointer");

    std::println("--- PASS: test_data_encodings_base64_hex_csv");
}

void test_relocatable_cmake_ejection([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_relocatable_cmake_ejection");
    ScopedTempDir tmp_eject_scope("cfire_tmp_eject");  // hermetic, mkdtemp-backed (CF-153)
    auto tmp_eject = tmp_eject_scope.path / "cfire_relocatable_eject";
    std::filesystem::remove_all(tmp_eject);
    auto ej = cfire::build::eject_cmake("examples/08-multi-pkg-cc-build/main.cf", rt_dir, tmp_eject);
    require(ej.has_value(), "eject_cmake for 08-multi-pkg-cc-build");
    require(std::filesystem::exists(tmp_eject / "vendor" / "FastCRC" / "fastcrc.c"),
        "vendor companion tree FastCRC mirrored into ejected folder");
    auto cmake_content = cfire::build::read_file_text(tmp_eject / "CMakeLists.txt");
    require(cmake_content.has_value() && cmake_content->contains("vendor/FastCRC/fastcrc.c"),
        "CMakeLists.txt references relocatable vendor relative path");

    // Test CMake configure if cmake binary exists on system
    int cmake_check = 0;
    auto _cmake_out = cfire::bridge::run_command_capture("which cmake", cmake_check);
    (void)_cmake_out;
    if (cmake_check == 0) {
        int build_rc = 0;
        std::string cmd = std::format("cmake -S \"{}\" -B \"{}/build\" && cmake --build \"{}/build\"",
            tmp_eject.string(), tmp_eject.string(), tmp_eject.string());
        std::string build_out = cfire::bridge::run_command_capture(cmd, build_rc);
        require(build_rc == 0, std::format("ejected CMake project builds standalone: {}", build_out));
    }
    std::filesystem::remove_all(tmp_eject);
    std::println("--- PASS: test_relocatable_cmake_ejection");
}

void test_variant_pattern_matching([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_variant_pattern_matching");
    std::string_view src = R"(package main

type Item enum {
Named(str)
Empty
}

fn consume_variant(move it: Item) -> str {
match move it {
    Named(val) => {
        return move val
    }
    Empty => {
        return "empty"
    }
}
}

fn main() {
}
)";
    auto ast = cfire::frontend::parse_source("test_match_move.cf", src);
    require(ast.has_value(), std::format("parse match move item: {}", ast ? "" : ast.error()));
    if (ast) {
        auto checked = cfire::frontend::analyze(std::move(*ast));
        require(checked.has_value(), std::format("analyze match move item: {}", checked ? "" : checked.error()));
        if (checked) {
            std::string cpp = cfire::frontend::emit_cpp(*checked);
            require(cpp.contains("std::get_if<Item::_Named>(&_target._v)"),
                "match move emits std::get_if for variant");
            require(cpp.contains("auto&& val = std::move(_p->_0);"),
                "match move binds payload as rvalue reference with std::move");
            require(cpp.contains("auto&& _target = std::move(it);"),
                "match receives std::move(it)");
        }
    }
    std::println("--- PASS: test_variant_pattern_matching");
}

void test_mutation_exclusivity_path_alias([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_mutation_exclusivity_path_alias");
    // 1. Conflicting projection path: mut pt.x, pt.x -> ERROR
    std::string_view src_conflict = R"(package main

type Point struct {
x: i32
y: i32
}

fn update(mut a: i32, b: i32) {}

fn run_test() {
mut pt := Point{x: 1, y: 2}
update(mut pt.x, pt.x)
}
)";
    auto ast1 = cfire::frontend::parse_source("test_alias_conflict.cf", src_conflict);
    require(ast1.has_value(), std::format("parse test_alias_conflict.cf: {}", ast1 ? "" : ast1.error()));
    if (ast1) {
        auto chk = cfire::frontend::analyze(std::move(*ast1));
        require(!chk.has_value(), "aliasing mut pt.x with pt.x must fail");
        if (!chk) {
            require(chk.error().contains("cfire/MUT-ALIAS"),
                "projection aliasing reports cfire/MUT-ALIAS");
        }
    }

    // 2. Conflicting root with child: mut pt, pt.x -> ERROR
    std::string_view src_root_conflict = R"(package main

type Point struct {
x: i32
y: i32
}

fn update_pt(mut p: Point, v: i32) {}

fn run_test() {
mut pt := Point{x: 1, y: 2}
update_pt(mut pt, pt.x)
}
)";
    auto ast2 = cfire::frontend::parse_source("test_root_conflict.cf", src_root_conflict);
    require(ast2.has_value(), std::format("parse test_root_conflict.cf: {}", ast2 ? "" : ast2.error()));
    if (ast2) {
        auto chk = cfire::frontend::analyze(std::move(*ast2));
        require(!chk.has_value(), "aliasing mut pt with pt.x must fail");
        if (!chk) {
            require(chk.error().contains("cfire/MUT-ALIAS"),
                "root with child projection reports cfire/MUT-ALIAS");
        }
    }

    // 3. Sibling non-overlapping paths: mut pt.x, pt.y -> OK
    std::string_view src_siblings = R"(package main

type Point struct {
x: i32
y: i32
}

fn swap_coords(mut a: i32, b: i32) {}

fn run_test() {
mut pt := Point{x: 1, y: 2}
swap_coords(mut pt.x, pt.y)
}
)";
    auto ast3 = cfire::frontend::parse_source("test_siblings.cf", src_siblings);
    require(ast3.has_value(), std::format("parse test_siblings.cf: {}", ast3 ? "" : ast3.error()));
    if (ast3) {
        auto chk = cfire::frontend::analyze(std::move(*ast3));
        require(chk.has_value(), "distinct non-overlapping sibling projections are permitted");
    }
    std::println("--- PASS: test_mutation_exclusivity_path_alias");
}

void test_preprocessor_macro_injection([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_preprocessor_macro_injection");
    std::string_view src = R"(package main

import cpp "stb_image.h" @define("STB_IMAGE_IMPLEMENTATION") @define("BUFFER_SIZE", "4096")

fn main() {}
)";
    auto ast = cfire::frontend::parse_source("test_macro_def.cf", src);
    require(ast.has_value(), std::format("parse @define attributes: {}", ast ? "" : ast.error()));
    if (ast) {
        auto checked = cfire::frontend::analyze(std::move(*ast));
        require(checked.has_value(), "analyze @define attributes");
        if (checked) {
            std::string cpp = cfire::frontend::emit_cpp(*checked);
            require(cpp.contains("#define STB_IMAGE_IMPLEMENTATION\n#define BUFFER_SIZE 4096\n#include <stb_image.h>"),
                "macro definitions emitted immediately before C++ #include");
        }
    }
    std::println("--- PASS: test_preprocessor_macro_injection");
}

void test_regex_matching_and_captures([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_regex_matching_and_captures");
    auto re = cfire::regex::compile(R"(\d+)");
    require(re.has_value(), "compile valid regex");
    if (re) {
        require(re->is_match("user 1234 active"), "regex is_match positive");
        require(!re->is_match("user inactive"), "regex is_match negative");
        auto first = re->find("id: 42, count: 99");
        require(first.has_value() && *first == "42", "regex find first match");
        auto all = re->find_all("10 items, 20 boxes, 30 pallets");
        require(all.size() == 3 && all[0] == "10" && all[1] == "20" && all[2] == "30",
            "regex find_all matches");
        auto replaced = re->replace_all("width: 100, height: 200", "0");
        require(replaced == "width: 0, height: 0", "regex replace_all");
    }
    auto bad = cfire::regex::compile("[unclosed-group");
    require(!bad.has_value(), "compile invalid regex returns error");

    auto m_quick = cfire::regex::is_match(R"(^[a-zA-Z]+$)", "CfireLanguage");
    require(m_quick.has_value() && *m_quick, "regex package-level is_match helper");

    auto r_quick = cfire::regex::replace_all(R"(\s+)", "apple   banana   orange", " ");
    require(r_quick.has_value() && *r_quick == "apple banana orange", "regex package-level replace_all helper");
    std::println("--- PASS: test_regex_matching_and_captures");
}

void test_collections_and_in_place_sort([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_collections_and_in_place_sort");
    // 1. Set[T]
    cfire::collections::Set<std::string> set;
    require(set.empty(), "set starts empty");
    require(set.insert("alpha"), "set insert new key");
    require(!set.insert("alpha"), "set insert duplicate returns false");
    require(set.size() == 1, "set size is 1");
    require(set.contains("alpha"), "set contains existing key");
    require(!set.contains("beta"), "set does not contain missing key");
    require(set.remove("alpha"), "set remove key returns true");
    require(!set.contains("alpha"), "set removed key no longer present");

    // 2. PriorityQueue[T] (Max-Heap)
    cfire::collections::PriorityQueue<int> pq;
    pq.push(20);
    pq.push(50);
    pq.push(10);
    pq.push(30);
    require(pq.size() == 4, "priority queue size is 4");
    require(pq.peek() == 50, "priority queue top is max element");
    require(pq.pop() == 50, "pop max element 50");
    require(pq.pop() == 30, "pop next element 30");
    require(pq.pop() == 20, "pop next element 20");
    require(pq.pop() == 10, "pop final element 10");
    require(pq.pop() == std::nullopt, "pop from empty queue returns nullopt");

    // 3. Deque[T] (Double-ended queue)
    cfire::collections::Deque<int> dq;
    dq.push_back(20);
    dq.push_front(10);
    dq.push_back(30);
    require(dq.size() == 3, "deque size is 3");
    require(dq.at(0) == 10 && dq.at(1) == 20 && dq.at(2) == 30, "deque indexing order");
    require(dq.pop_front() == 10, "pop_front yields 10");
    require(dq.pop_back() == 30, "pop_back yields 30");
    require(dq.pop_back() == 20, "pop_back yields 20");
    require(dq.empty(), "deque empty after draining");

    // 4. In-Place Sorting & Binary Search
    std::vector<int> vals = {42, 12, 88, 3, 55, 19};
    cfire::sort::slice(vals);
    require(vals == std::vector<int>{3, 12, 19, 42, 55, 88}, "sort.slice sorts in ascending order");

    auto pos = cfire::sort::binary_search(std::span<const int>(vals), 42);
    require(pos.has_value() && *pos == 3, "sort.binary_search finds existing key");
    auto missing = cfire::sort::binary_search(std::span<const int>(vals), 50);
    require(!missing.has_value(), "sort.binary_search returns nullopt on missing element");
    std::println("--- PASS: test_collections_and_in_place_sort");
}

void test_generic_call_disambiguation([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_generic_call_disambiguation");
    std::string_view src = R"(package main

fn pair[T, U](a: T, b: U) -> T {
return a
}

fn test_generics() {
p := pair[i32, str](42, "hello")
s := pair[[]i32, bool]([1, 2], true)
}

fn main() {}
)";
    auto ast = cfire::frontend::parse_source("test_multi_generics.cf", src);
    require(ast.has_value(), std::format("parse multi-argument generic calls f[T, U](args): {}", ast ? "" : ast.error()));
    if (ast) {
        auto checked = cfire::frontend::analyze(std::move(*ast));
        require(checked.has_value(), "analyze multi-argument generic calls");
        if (checked) {
            std::string cpp = cfire::frontend::emit_cpp(*checked);
            require(cpp.contains("pair<std::int32_t, std::string>(42, \"hello\")"),
                "codegen lowers multi-type generic call f<T1, T2>(args)");
            require(cpp.contains("pair<std::vector<std::int32_t>, bool>("),
                "codegen lowers compound-type generic call with slice type arg");
        }
    }
    std::println("--- PASS: test_generic_call_disambiguation");
}

void test_time_durations_and_calendar([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_time_durations_and_calendar");
    auto d5 = cfire::time::Duration::from_secs(5);
    require(d5.millis() == 5000, "Duration millis");
    require(d5.micros() == 5000000, "Duration micros");
    require(d5.nanos == 5000000000LL, "Duration nanos");

    auto d_sum = cfire::time::Duration::from_millis(250) + cfire::time::Duration::from_millis(750);
    require(d_sum.millis() == 1000, "Duration operator+");
    require(d_sum == cfire::time::Duration::from_secs(1), "Duration equality");

    auto now = cfire::time::now();
    require(now.unix_seconds() > 1700000000LL, "time.now() unix_seconds");

    auto t1 = cfire::time::from_unix_seconds(1700000000LL);
    auto formatted = t1.format_rfc3339();
    require(formatted == "2023-11-14T22:13:20Z", "format_rfc3339 matches expected");

    auto parsed = cfire::time::parse_rfc3339("2023-11-14T22:13:20Z");
    require(parsed.has_value(), "parse_rfc3339 roundtrip succeeds");
    if (parsed) {
        require(parsed->unix_seconds() == 1700000000LL, "parsed timestamp matches original");
        require(parsed->year() == 2023, "calendar breakdown year");
        require(parsed->month() == 11, "calendar breakdown month");
        require(parsed->day() == 14, "calendar breakdown day");
        require(parsed->hour() == 22, "calendar breakdown hour");
        require(parsed->minute() == 13, "calendar breakdown minute");
        require(parsed->second() == 20, "calendar breakdown second");
    }

    auto bad_parse = cfire::time::parse_rfc3339("invalid-timestamp-string");
    require(!bad_parse.has_value(), "parse_rfc3339 returns error on malformed string");
    std::println("--- PASS: test_time_durations_and_calendar");
}

void test_structured_logging_json_and_text([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_structured_logging_json_and_text");
    cfire::log::set_level(cfire::log::Level::Debug);
    require(cfire::log::get_level() == cfire::log::Level::Debug, "log get/set level");

    cfire::log::set_format(cfire::log::Format::Json);
    require(cfire::log::get_format() == cfire::log::Format::Json, "log get/set format JSON");
    cfire::log::info("user logged in", "user_id", "42", "tenant", "prod");

    cfire::log::set_format(cfire::log::Format::Text);
    require(cfire::log::get_format() == cfire::log::Format::Text, "log get/set format Text");
    cfire::log::debug("debugging details", "status", "ok");
    cfire::log::warn("resource approaching limit", "usage_pct", "85");
    cfire::log::error("service unreachable", "host", "backend.local");
    std::println("--- PASS: test_structured_logging_json_and_text");
}

void test_shared_library_dynamic_linking([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_shared_library_dynamic_linking");
    ScopedTempDir tmp_bs06_scope("cfire_tmp_bs06");  // hermetic, mkdtemp-backed (CF-153)
    auto tmp_bs06 = tmp_bs06_scope.path / "cfire_bs06_test";
    std::filesystem::remove_all(tmp_bs06);
    std::filesystem::create_directories(tmp_bs06 / "vendor" / "sharedlib");

    {
        std::ofstream c_out(tmp_bs06 / "vendor" / "sharedlib" / "shared_math.c");
        c_out << "int add_numbers(int a, int b) { return a + b; }\n";
    }

    cfire::build::BuildPlan plan;
    cfire::build::ExternalDepSpec dep;
    dep.name = "shared_math";
    dep.driver = "CC";
    dep.source = "vendor/sharedlib";
    dep.is_shared = true;

    auto res = cfire::build::build_local_dep(tmp_bs06, dep, plan);
    require(res.has_value(), std::format("build_local_dep produces shared library: {}", res ? "" : res.error()));
    require(dep.is_shared, "BS-06: dep.is_shared confirmed");
    require(!dep.built_archive.empty(), "BS-06: built_archive path populated");
#if defined(__APPLE__)
    require(dep.built_archive.ends_with(".dylib"), "BS-06: macOS produces .dylib");
#else
    require(dep.built_archive.ends_with(".so"), "BS-06: Linux produces .so");
#endif
    bool has_rpath = false;
    for (const auto& f : plan.ldflags) {
        if (f.starts_with("-Wl,-rpath,")) {
            has_rpath = true;
            break;
        }
    }
    require(has_rpath, "BS-06: automatic -Wl,-rpath added to plan.ldflags for shared library");
    std::println("--- PASS: test_shared_library_dynamic_linking");
}

void test_multi_return_tuples_and_destructuring([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_multi_return_tuples_and_destructuring");
    // 1. Parsing & Codegen of Multi-Return Tuple and Destructuring
    std::string_view src = R"(package main

fn coords() -> (f64, f64)! {
return (10.5, 20.5)
}

fn main() {
(x, y) := try coords()
mut a := 0.0
mut b := 0.0
(a, b) = (x, y)
}
)";
    auto ast = cfire::frontend::parse_source("test_tuples.cf", src);
    require(ast.has_value(), std::format("parse tuple syntax: {}", ast ? "" : ast.error()));
    if (ast) {
        auto checked = cfire::frontend::analyze(std::move(*ast));
        require(checked.has_value(), "analyze tuple syntax");
        if (checked) {
            std::string cpp = cfire::frontend::emit_cpp(*checked);
            require(cpp.contains("auto coords() -> std::expected<std::tuple<double, double>, cfire::Error>"),
                "codegen lowers multi-return tuple to std::expected<std::tuple<...>>");
            require(cpp.contains("return std::make_tuple(10.5, 20.5);"),
                "codegen lowers tuple literal to std::make_tuple");
            require(cpp.contains("auto [x, y] ="),
                "codegen lowers deconstructing var decl to C++23 structured binding");
            require(cpp.contains("std::tie(a, b) = std::make_tuple(x, y);"),
                "codegen lowers deconstructing assign to std::tie");
        }
    }

    // 2. Deprecation warning for 'out' parameters
    // Q33 (CF-175): `out` is not a parameter intent any more (multi-return tuples replace it).
    require(!cfire::frontend::parse_source("test_out_param.cf", "package main\nfn legacy_div(num: i32, denom: i32, out rem: i32) -> i32 {\n    return num / denom\n}\nfn main() {}\n").has_value(),
        "out parameter intent no longer parses (CF-175)");
    std::println("--- PASS: test_multi_return_tuples_and_destructuring");
}

void test_filesystem_streaming_and_paths([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_filesystem_streaming_and_paths");
    // 1. Path decomposition helpers
    require(cfire::fs::dirname("/usr/local/bin/cfire") == "/usr/local/bin", "fs.dirname");
    require(cfire::fs::basename("/usr/local/bin/cfire") == "cfire", "fs.basename");
    require(cfire::fs::extension("/usr/local/bin/cfire.tar.gz") == ".gz", "fs.extension");
    require(cfire::fs::extension("source.cf") == ".cf", "fs.extension .cf");

    // 2. Streaming File open, write, seek, tell, read, flush, close
    ScopedTempDir tmp_file_scope("cfire_tmp_file");  // hermetic, mkdtemp-backed (CF-153)
    auto tmp_file = tmp_file_scope.path / "cfire_stream_test.txt";
    std::filesystem::remove(tmp_file);

    auto file_res = cfire::fs::open(tmp_file, cfire::fs::OpenMode::Write);
    require(file_res.has_value(), "fs.open for write");
    if (file_res) {
        auto& f = *file_res;
        require(f.is_open(), "file is open");
        auto w1 = f.write_text("Hello Streaming World!\nLine 2.");
        require(w1.has_value() && *w1 > 0, "file.write_text");
        auto fl = f.flush();
        require(fl.has_value(), "file.flush");
        f.close();
        require(!f.is_open(), "file closed");
    }

    // Reopen for read and seek
    auto r_res = cfire::fs::open(tmp_file, cfire::fs::OpenMode::Read);
    require(r_res.has_value(), "fs.open for read");
    if (r_res) {
        auto& f = *r_res;
        auto t0 = f.tell();
        require(t0.has_value() && *t0 == 0, "file.tell at start");

        // Seek to offset 6 ("Streaming")
        auto s1 = f.seek(6, cfire::fs::SeekWhence::Start);
        require(s1.has_value() && *s1 == 6, "file.seek Start");

        std::vector<uint8_t> chunk(9);
        auto n_read = f.read_into(chunk);
        require(n_read.has_value() && *n_read == 9, "file.read_into 9 bytes");
        std::string word(reinterpret_cast<const char*>(chunk.data()), chunk.size());
        require(word == "Streaming", "file chunk content matches");

        // Seek back to start and read_bytes
        auto s0 = f.seek(0, cfire::fs::SeekWhence::Start);
        require(s0.has_value() && *s0 == 0, "file.seek to 0");
        auto all_bytes = f.read_bytes();
        require(all_bytes.has_value(), "file.read_bytes");
        std::string all_str(reinterpret_cast<const char*>(all_bytes->data()), all_bytes->size());
        require(all_str == "Hello Streaming World!\nLine 2.", "file.read_bytes content matches");

        // Seek to start and test read_exact, read_text_until, read_lines
        (void)f.seek(0, cfire::fs::SeekWhence::Start);
        auto exact5 = f.read_exact(5);
        require(exact5.has_value() && cfire::to_str(*exact5) == "Hello", "file.read_exact(5) == Hello");
        auto until_nl = f.read_text_until("\n");
        require(until_nl.has_value() && *until_nl == " Streaming World!\n", "file.read_text_until('\\n')");
        (void)f.seek(0, cfire::fs::SeekWhence::Start);
        auto lines = f.read_lines();
        require(lines.has_value() && lines->size() == 2 && (*lines)[0] == "Hello Streaming World!" && (*lines)[1] == "Line 2.",
            "file.read_lines() returns 2 lines");
        f.close();
    }

    // 3. Test one-shot fs helpers: is_file, is_dir, file_size, append_text, append_bytes, copy_file
    require(cfire::fs::is_file(tmp_file), "fs.is_file(tmp_file)");
    require(!cfire::fs::is_dir(tmp_file), "!fs.is_dir(tmp_file)");
    require(cfire::fs::is_dir(std::filesystem::temp_directory_path()), "fs.is_dir(temp_directory_path)");
    require(cfire::fs::append_text(tmp_file, "\nLine 3.").has_value(), "fs.append_text");
    ScopedTempDir copy_path_scope("cfire_copy_path");  // hermetic, mkdtemp-backed (CF-153)
    auto copy_path = copy_path_scope.path / "cfire_stream_copy.txt";
    require(cfire::fs::copy_file(tmp_file, copy_path).has_value(), "fs.copy_file");
    auto sz = cfire::fs::file_size(copy_path);
    require(sz.has_value() && *sz == 38, "fs.file_size matches appended length");
    std::filesystem::remove(copy_path);
    std::filesystem::remove(tmp_file);
    std::println("--- PASS: test_filesystem_streaming_and_paths");
}

void test_json_dynamic_dom_and_serializer([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_json_dynamic_dom_and_serializer");
    // 1. String escaping
    std::string raw_esc = "quote: \", backslash: \\, newline: \n, tab: \t";
    std::string escaped = cfire::json::escape_string(raw_esc);
    require(escaped.contains("\\\""), "escaped quote");
    require(escaped.contains("\\\\"), "escaped backslash");
    require(escaped.contains("\\n"), "escaped newline");
    require(escaped.contains("\\t"), "escaped tab");

    // 2. Dynamic JSON DOM parsing
    std::string json_input = R"({
        "service": "cfire-auth",
        "version": 2,
        "rate_limit": 1500.5,
        "active": true,
        "metadata": null,
        "tags": ["core", "security", "p2"],
        "config": {
            "timeout_ms": 5000,
            "domain": "auth.internal"
        }
    })";

    auto dom_res = cfire::json::parse_dynamic(json_input);
    require(dom_res.has_value(), std::format("parse_dynamic: {}", dom_res ? "" : dom_res.error().message()));
    if (dom_res) {
        const auto& root = *dom_res;
        require(root.is_object(), "root is object");
        require(root["service"].is_string() && root["service"].as_str() == "cfire-auth", "get string");
        require(root["version"].is_int() && root["version"].as_int() == 2, "get int");
        require(root["rate_limit"].is_float() && std::abs(root["rate_limit"].as_float() - 1500.5) < 0.001, "get float");
        require(root["active"].is_bool() && root["active"].as_bool() == true, "get bool");
        require(root["metadata"].is_null(), "get null");

        auto tags = root["tags"];
        require(tags.is_array() && tags.size() == 3, "tags is array of size 3");
        require(tags[0].as_str() == "core", "tags[0] == core");
        require(tags[2].as_str() == "p2", "tags[2] == p2");

        auto cfg = root["config"];
        require(cfg.is_object(), "nested config is object");
        require(cfg["timeout_ms"].as_int() == 5000, "nested config timeout_ms");
        require(cfg["domain"].as_str() == "auth.internal", "nested config domain");

        // Dynamic stringify roundtrip
        std::string serialized = cfire::json::stringify_dynamic(root);
        auto re_parsed = cfire::json::parse_dynamic(serialized);
        require(re_parsed.has_value(), "re-parse serialized dynamic json");
        if (re_parsed) {
            require((*re_parsed)["service"].as_str() == "cfire-auth", "roundtrip value match");
            require((*re_parsed)["tags"].size() == 3, "roundtrip array size match");
        }
    }
    std::println("--- PASS: test_json_dynamic_dom_and_serializer");
}

void test_variable_declarations_unification([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_variable_declarations_unification");
    // 1. Canonical package-level & local declarations
    std::string_view src_canonical = R"(package main

app_name := "SapphireServer"
mut port := 8080
max_conn: i32 = 1000
mut is_ready: bool = true

fn test_vars() {
x := 10
mut y := 20
name: str = "cfire"
mut count: i64 = 99
}
)";
    auto ast = cfire::frontend::parse_source("test_canonical_vars.cf", src_canonical);
    require(ast.has_value(), std::format("parse canonical package and local vars: {}", ast ? "" : ast.error()));
    if (ast) {
        auto checked = cfire::frontend::analyze(std::move(*ast));
        require(checked.has_value(), "analyze canonical vars");
        if (checked) {
            require(checked->warnings.empty(), "canonical declarations emit zero warnings");
            std::string cpp = cfire::frontend::emit_cpp(*checked);
            require(cpp.contains("inline const auto app_name = \"SapphireServer\";"), "codegen package const inferred var");
            require(cpp.contains("inline auto port = 8080;"), "codegen package mut inferred var");
            require(cpp.contains("inline const std::int32_t max_conn = 1000;"), "codegen package const typed var");
            require(cpp.contains("inline bool is_ready = true;"), "codegen package mut typed var");
        }
    }

    // 2. Q33 (CF-175): `var` is an identifier, not a declaration keyword.
    require(!cfire::frontend::parse_source("test_deprecated_var.cf", "package main\nvar global_legacy = 123\n").has_value(),
        "`var x = ...` is not a declaration form (CF-175)");
    std::println("--- PASS: test_variable_declarations_unification");
}

void test_recursive_package_resolution([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_recursive_package_resolution");
    ScopedTempDir tmp_ws("cfire_recursive_pkg");

    // pkg_b contains a C library dependency in its sibling build.cf
    tmp_ws.file("pkg_b/pkg_b.cf", R"(package pkg_b
fn helper_b() -> i32 { return 42 }
)");
    tmp_ws.file("pkg_b/build.cf", R"(package build
helper_lib := build.CC("helper_c", {
Sources: ["helper.c"]
})
)");
    tmp_ws.file("pkg_b/helper.c", "int helper_c_fn() { return 100; }\n");

    // pkg_a imports pkg_b
    tmp_ws.file("pkg_a/pkg_a.cf", R"(package pkg_a
import "../pkg_b"
fn helper_a() -> i32 { return pkg_b.helper_b() + 1 }
)");

    // root main.cf imports pkg_a
    auto main_cf = tmp_ws.file("main.cf", R"(package main
import "./pkg_a"
fn main() -> i32 { return pkg_a.helper_a() }
)");

    auto res = cfire::build::analyze_and_emit(main_cf, rt_dir, false);
    require(res.has_value(), std::format("analyze_and_emit recursive packages: {}", res ? "" : res.error()));
    if (res) {
        const auto& plan = res->second;
        bool found_b_dep = false;
        for (const auto& dep : plan.external_deps) {
            if (dep.name == "helper_lib") found_b_dep = true;
        }
        require(found_b_dep, "subpackage build.cf external dependency aggregated into root build plan");
    }
    std::println("--- PASS: test_recursive_package_resolution");
}

void test_pre_build_codegen_hooks([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_pre_build_codegen_hooks");
    ScopedTempDir tmp_hook_ws("cfire_prebuild_hook");

    // build.cf declaring pre-build Command
    tmp_hook_ws.file("build.cf", R"(package build
gen_hook := build.Command("/bin/sh", {
Args: ["-c", "echo 'package main' > generated.cf && echo 'fn gen_code() -> i32 { return 777 }' >> generated.cf"],
Outputs: ["generated.cf"]
})
)");

    auto main_cf = tmp_hook_ws.file("main.cf", R"(package main
import "./generated.cf"
fn main() -> i32 { return gen_code() }
)");

    auto res = cfire::build::analyze_and_emit(main_cf, rt_dir, false);
    require(res.has_value(), std::format("analyze_and_emit with pre-build command hook: {}", res ? "" : res.error()));
    require(std::filesystem::exists(tmp_hook_ws.path / "generated.cf"), "pre-build hook generated target file");
    std::println("--- PASS: test_pre_build_codegen_hooks");
}

void test_unicode_utf8_runes_and_case([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_unicode_utf8_runes_and_case");
    std::string ascii = "Hello, world!";
    require(cfire::unicode::rune_count(ascii) == 13, "ascii rune count");
    require(cfire::unicode::is_valid_utf8(ascii), "ascii is valid utf8");

    std::string multi = "Hello, 世界! 🚀";
    // "Hello, " (7) + "世界" (2) + "! " (2) + "🚀" (1) = 12 runes, but 19 bytes!
    require(cfire::unicode::rune_count(multi) == 12, "multibyte utf8 rune count");
    require(multi.size() == 19, "byte size is 19");
    require(cfire::unicode::is_valid_utf8(multi), "multibyte is valid utf8");

    auto cps = cfire::unicode::runes(multi);
    require(cps.size() == 12, "runes vector size matches rune count");
    require(cps[7] == 0x4E16, "codepoint for 世");
    require(cps[8] == 0x754C, "codepoint for 界");
    require(cps[11] == 0x1F680, "codepoint for rocket emoji 🚀");

    // Encoding rune
    std::string encoded = cfire::unicode::encode_rune(0x1F680);
    require(encoded == "🚀", "encode_rune roundtrip");

    // Invalid byte sequence rejection
    std::string invalid_bytes = "abc\xFF\xFE" "def";
    require(!cfire::unicode::is_valid_utf8(invalid_bytes), "invalid utf8 sequence rejected");

    // Case transformation (ASCII + Cyrillic)
    std::string mixed_cyr = "Hello, МИР!";
    std::string lower_cyr = cfire::unicode::to_lower(mixed_cyr);
    require(lower_cyr == "hello, мир!", "unicode to_lower");
    std::string upper_cyr = cfire::unicode::to_upper(lower_cyr);
    require(upper_cyr == "HELLO, МИР!", "unicode to_upper");
    std::println("--- PASS: test_unicode_utf8_runes_and_case");
}

void test_struct_tags_and_metadata([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_struct_tags_and_metadata");
    std::string_view src = R"(package main

type ServerConfig struct {
port: i32       `json:"port" env:"PORT" cli:"--port,-p" help:"Listening port"`
host: str       `json:"host" env:"HOST" yaml:"server_host"`
max_conn: i64   `json:"max_conn" db:"max_connections" min:"1" max:"10000"`
}

fn main() {}
)";
    auto ast = cfire::frontend::parse_source("test_tags.cf", src);
    require(ast.has_value(), std::format("parse generalized struct tags: {}", ast ? "" : ast.error()));
    if (ast) {
        require(!ast->structs.empty(), "parsed struct");
        const auto& st = ast->structs[0];
        require(st.fields.size() == 3, "struct has 3 fields");
        
        // Check field 0 tags
        const auto& f0 = st.fields[0];
        require(f0.tags.size() == 4, "field 0 has 4 parsed tags");
        require(f0.get_tag("json") == "port", "get_tag json");
        require(f0.get_tag("env") == "PORT", "get_tag env");
        require(f0.get_tag("cli") == "--port,-p", "get_tag cli");
        require(f0.get_tag("help") == "Listening port", "get_tag help");

        // Check field 1 custom yaml tag
        const auto& f1 = st.fields[1];
        require(f1.get_tag("yaml") == "server_host", "get_tag yaml custom tag");

        // Check field 2 custom min/max tags
        const auto& f2 = st.fields[2];
        require(f2.get_tag("min") == "1" && f2.get_tag("max") == "10000", "get_tag custom min/max");

        // Verify codegen emits reflection metadata
        auto chk = cfire::frontend::analyze(std::move(*ast));
        require(chk.has_value(), "analyze struct with custom tags");
        if (chk) {
            std::string cpp = cfire::frontend::emit_cpp(*chk);
            require(cpp.contains("template <>"), "emits template specialization");
            require(cpp.contains("struct cfire::reflect_struct<ServerConfig> {"), "emits reflect_struct specialization");
            require(cpp.contains("static constexpr std::string_view name = \"ServerConfig\";"), "reflect_struct name");
            require(cpp.contains("static constexpr std::size_t field_count = 3;"), "reflect_struct field_count 3");
            require(cpp.contains("\"port\","), "reflect_struct field_names port");
            require(cpp.contains("\"host\","), "reflect_struct field_names host");
        }
    }
    std::println("--- PASS: test_struct_tags_and_metadata");
}

#include "test_stdlib_tooling_fuzz_bench.hpp"
#include "test_stdlib_net_sync.hpp"
#include "test_stdlib_webui_ext.hpp"
