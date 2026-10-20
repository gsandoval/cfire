#include "../test_harness.hpp"

void test_crypto_sha256_and_csprng([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_crypto_sha256_and_csprng");
    // Standard NIST FIPS 180-4 SHA-256 test vectors:
    auto h_empty = cfire::sha256::hash_hex("");
    require(h_empty == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "sha256 empty string test vector");

    auto h_abc = cfire::sha256::hash_hex("abc");
    require(h_abc == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "sha256 'abc' NIST test vector");

    auto b_abc = cfire::sha256::hash_bytes(std::span<const std::byte>(reinterpret_cast<const std::byte*>("abc"), 3));
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
        require(res->stdout_str.contains("hello world arg with spaces"),
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
    auto cache_tmp = std::filesystem::temp_directory_path() / "cfire_bs03_test.idx";
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
mut val := 10
ptr := &mut val
async.scope(workers: 2) |scope| {
    defer set_val(ptr, 99)
    scope.spawn || {
        *ptr = 42
    }
}
return val
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
            require(cpp.contains("std::string_view borrow_str"),
                "&str parameter lowers to std::string_view");
            require(cpp.contains("const std::string& owned_str"),
                "owned str parameter lowers to const std::string&");
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
    std::println("--- PASS: test_data_encodings_base64_hex_csv");
}

void test_relocatable_cmake_ejection([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_relocatable_cmake_ejection");
    auto tmp_eject = std::filesystem::temp_directory_path() / "cfire_relocatable_eject";
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
            require(cpp.contains("[&](Item::_Named&& _m)"),
                "match move emits rvalue reference visitor");
            require(cpp.contains("auto&& val = std::move(_m._0);"),
                "match move binds payload as rvalue reference with std::move");
            require(cpp.contains("std::move(it._v)"),
                "std::visit receives std::move(it._v)");
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
    auto tmp_bs06 = std::filesystem::temp_directory_path() / "cfire_bs06_test";
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
    std::string_view src_out = R"(package main

fn legacy_div(num: i32, denom: i32, out rem: i32) -> i32 {
rem = num % denom
return num / denom
}

fn main() {
mut r := 0
q := legacy_div(10, 3, out r)
}
)";
    auto ast_out = cfire::frontend::parse_source("test_out_param.cf", src_out);
    require(ast_out.has_value(), "parse out parameter code");
    if (ast_out) {
        auto checked = cfire::frontend::analyze(std::move(*ast_out));
        require(checked.has_value(), "analyze out parameter code succeeds with warnings");
        if (checked) {
            bool has_dep_warn = std::ranges::any_of(checked->warnings, [](const auto& w) {
                return w.contains("cfire/DEPRECATED-OUT-PARAM");
            });
            require(has_dep_warn, "deprecation warning emitted for 'out' parameter");
        }
    }
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
    auto tmp_file = std::filesystem::temp_directory_path() / "cfire_stream_test.txt";
    std::filesystem::remove(tmp_file);

    auto file_res = cfire::fs::open(tmp_file, cfire::fs::OpenMode::Write);
    require(file_res.has_value(), "fs.open for write");
    if (file_res) {
        auto& f = *file_res;
        require(f.is_open(), "file is open");
        auto w1 = f.write_str("Hello Streaming World!\nLine 2.");
        require(w1.has_value() && *w1 > 0, "file.write_str");
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
        auto n_read = f.read(chunk);
        require(n_read.has_value() && *n_read == 9, "file.read 9 bytes");
        std::string word(reinterpret_cast<const char*>(chunk.data()), chunk.size());
        require(word == "Streaming", "file chunk content matches");

        // Seek back to start and read_all
        auto s0 = f.seek(0, cfire::fs::SeekWhence::Start);
        require(s0.has_value() && *s0 == 0, "file.seek to 0");
        auto all_bytes = f.read_all();
        require(all_bytes.has_value(), "file.read_all");
        std::string all_str(reinterpret_cast<const char*>(all_bytes->data()), all_bytes->size());
        require(all_str == "Hello Streaming World!\nLine 2.", "file.read_all content matches");
        f.close();
    }
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

    // 2. Deprecation warning for 'var'
    std::string_view src_var = R"(package main

var global_legacy = 123
var mut global_typed: i32 = 456

fn legacy_local() {
var a = 1
var mut b: str = "old"
}
)";
    auto ast_var = cfire::frontend::parse_source("test_deprecated_var.cf", src_var);
    require(ast_var.has_value(), "parse legacy var code");
    if (ast_var) {
        auto checked = cfire::frontend::analyze(std::move(*ast_var));
        require(checked.has_value(), "analyze legacy var code succeeds with warnings");
        if (checked) {
            int warn_count = 0;
            for (const auto& w : checked->warnings) {
                if (w.contains("cfire/DEPRECATED-VAR")) ++warn_count;
            }
            require(warn_count >= 4, "emits cfire/DEPRECATED-VAR warning for each var declaration");
        }
    }
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

void test_agent_tooling_outline_and_json([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_agent_tooling_outline_and_json");
    ScopedTempDir tmp_ws("cfire_tool");
    auto mod_cf = tmp_ws.file("mod.cf", R"(package mymod

import "core:fs"

type Config struct {
threads: i32 `json:"threads"`
}

type Status enum {
Active,
Idle
}

fn worker(cfg: Config) -> bool {
return true
}
)");

    // 1. Test outline generation
    run_cfire(std::format("outline --json \"{}\"", tmp_ws.path.string()))
        .assert_ok("cfire outline exit 0 (TOOL-01)")
        .assert_contains("\"package\": \"mymod\"", "outline contains package name (TOOL-01)")
        .assert_contains("\"imports\": [\"core:fs\"]", "outline contains imports (TOOL-01)")
        .assert_contains("\"name\": \"Config\"", "outline contains struct Config (TOOL-01)")
        .assert_contains("\"name\": \"Status\"", "outline contains enum Status (TOOL-01)")
        .assert_contains("\"name\": \"worker\"", "outline contains function worker (TOOL-01)");

    // 2. Test check --json
    run_cfire(std::format("check --json \"{}\"", mod_cf.string()))
        .assert_ok("cfire check --json exit 0 (TOOL-01)")
        .assert_contains("\"success\": true", "check --json reports success (TOOL-01)");

    // 3. Test doc --llm
    run_cfire("doc --llm")
        .assert_ok("cfire doc --llm exit 0 (TOOL-01)")
        .assert_contains("Reference Sheet", "doc --llm emits reference sheet (TOOL-01)")
        .assert_contains("core:unicode", "doc --llm lists core:unicode (TOOL-01)");
    std::println("--- PASS: test_agent_tooling_outline_and_json");
}

void test_ide_configuration_setup([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_ide_configuration_setup");
    ScopedTempDir tmp_ide_ws("cfire_ide");

    // Configure all IDEs
    auto res_all = cfire::lsp::configure_ide(tmp_ide_ws.path, "--all");
    require(res_all.has_value(), std::format("configure_ide --all (IDE-01): {}", res_all ? "" : res_all.error()));
    require(std::filesystem::exists(tmp_ide_ws.path / ".vscode" / "settings.json"), "creates .vscode/settings.json (IDE-01)");
    require(std::filesystem::exists(tmp_ide_ws.path / ".vscode" / "extensions.json"), "creates .vscode/extensions.json (IDE-01)");
    require(std::filesystem::exists(tmp_ide_ws.path / ".zed" / "settings.json"), "creates .zed/settings.json (IDE-01)");
    require(std::filesystem::exists(tmp_ide_ws.path / ".nvim.lua"), "creates .nvim.lua (IDE-01)");
    require(std::filesystem::exists(tmp_ide_ws.path / ".idea" / "lsp.xml"), "creates .idea/lsp.xml (IDE-01)");

    // Verify contents of .vscode/settings.json
    auto vs_txt = cfire::fs::read_text(tmp_ide_ws.path / ".vscode" / "settings.json");
    require(vs_txt.has_value() && vs_txt->contains("\"editor.defaultFormatter\": \"cfire\""), "vs settings formatter (IDE-01)");
    require(vs_txt->contains("\"cfire.inlayHints.enable\": true"), "vs settings inlay hints (IDE-01)");

    // Verify contents of .nvim.lua
    auto nvim_txt = cfire::fs::read_text(tmp_ide_ws.path / ".nvim.lua");
    require(nvim_txt.has_value() && nvim_txt->contains("vim.lsp.start"), "nvim lua starts lsp (IDE-01)");
    std::println("--- PASS: test_ide_configuration_setup");
}

void test_cfire_eval([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cfire_eval");

    // 1. Statement eval
    run_cfire("eval 'println(\"eval-test-ok\")'")
        .assert_ok("eval statement exit 0")
        .assert_contains("eval-test-ok", "eval statement stdout match");

    // 2. Expression eval (-e) (respects standard operator precedence: 100 + (42 * 2) = 184)
    run_cfire("eval -e '100 + 42 * 2'")
        .assert_ok("eval expression exit 0")
        .assert_contains("184", "eval expression stdout 184 match");

    // 3. Pre-imported stdlib (str + unicode)
    run_cfire("eval 'name := \"cfire🚀\"; println(\"upper={}, runes={}\", str.to_upper(\"cfire\"), unicode.rune_count(name))'")
        .assert_ok("eval pre-imported stdlib exit 0")
        .assert_contains("upper=CFIRE, runes=6", "eval pre-imported stdlib rune count match");

    // 4. Stdin piping
    int code = 0;
    std::string out4 = cfire::bridge::run_command_capture("echo 'println(\"pipe-ok-42\")' | ./cfire eval", code);
    require(code == 0, std::format("eval stdin piping exit 0: {}", out4));
    require(out4.contains("pipe-ok-42"), "eval stdin piping stdout match");

    // 5. Multi-line declarations (struct + helper function)
    run_cfire(R"(eval 'type Point struct {
    x: i32
    y: i32
}
fn double_point(p: Point) -> Point {
    return Point{ x: p.x * 2, y: p.y * 2 }
}
pt := double_point(Point{ x: 10, y: 20 })
println("x={}, y={}", pt.x, pt.y)')")
        .assert_ok("eval top-level decls exit 0")
        .assert_contains("x=20, y=40", "eval top-level struct/fn stdout match");

    // 6. Complete standalone package
    run_cfire(R"(eval 'package main
fn main() {
    println("full-pkg-ok")
}')")
        .assert_ok("eval complete package exit 0")
        .assert_contains("full-pkg-ok", "eval complete package stdout match");

    std::println("--- PASS: test_cfire_eval");
}



void test_cfire_remote_vendor(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_cfire_remote_vendor");
    (void)rt_dir;
    ScopedTempDir test_root("cfire_bs_ext");

    auto remote_repo = test_root.subpath("remote_calc");
    std::filesystem::create_directories(remote_repo);

    int rc = 0;
    (void)cfire::bridge::run_command_capture(
        std::format("git -C \"{}\" init -q && git -C \"{}\" config user.name \"Test\" && git -C \"{}\" config user.email \"test@example.com\"",
            remote_repo.string(), remote_repo.string(), remote_repo.string()), rc);
    require(rc == 0, "git init remote_calc");

    (void)cfire::fs::write_text(remote_repo / "calc.h",
        "#ifndef CALC_H\n#define CALC_H\n#ifdef __cplusplus\nextern \"C\" {\n#endif\nint add(int a, int b);\n#ifdef __cplusplus\n}\n#endif\n#endif\n");
    (void)cfire::fs::write_text(remote_repo / "calc.c",
        "#include \"calc.h\"\nint add(int a, int b) { return a + b; }\n");

    (void)cfire::bridge::run_command_capture(
        std::format("git -C \"{}\" add . && git -C \"{}\" commit -q -m \"Initial commit\"",
            remote_repo.string(), remote_repo.string()), rc);
    require(rc == 0, "git commit remote_calc");

    auto app_dir = test_root.subpath("app");
    std::filesystem::create_directories(app_dir);

    (void)cfire::fs::write_text(app_dir / "build.cf", std::format(
        "package build\n\nvar Calc = build.Git(\"{}\", {{\n    Driver: \"CC\",\n    Sources: [\"calc.c\"],\n    Includes: [\".\"]\n}})\n",
        remote_repo.string()));

    (void)cfire::fs::write_text(app_dir / "main.cf",
        "package main\n\nimport cpp \"calc.h\"\n\nfn main() {\n    res := cpp::add(15, 25)\n    println(\"sum={}\", res)\n}\n");

    // 1. Run cfire build/run and verify remote checkout + build.lock generation
    std::string run_cmd = std::format("cd \"{}\" && \"{}\"/cfire run main.cf",
        app_dir.string(), std::filesystem::current_path().string());
    std::string run_out = cfire::bridge::run_command_capture(run_cmd, rc);
    require(rc == 0, std::format("cfire run with Git remote dep exit 0: {}", run_out));
    require(run_out.contains("sum=40"), std::format("app output contains sum=40: {}", run_out));

    auto lock_file = app_dir / "build.lock";
    require(std::filesystem::exists(lock_file), "build.lock exists after run");
    auto lock_txt = cfire::build::read_file_text(lock_file);
    require(lock_txt.has_value(), "build.lock is readable");
    require(lock_txt->contains("\"packages\""), "build.lock contains packages");
    require(lock_txt->contains("\"Calc\""), "build.lock contains Calc");
    require(lock_txt->contains("\"commit\""), "build.lock contains commit hash");
    require(lock_txt->contains("\"driver\": \"CC\""), "build.lock contains driver CC");

    // 2. Vendor packages for offline air-gapped usage
    std::string vendor_cmd = std::format("cd \"{}\" && \"{}\"/cfire vendor",
        app_dir.string(), std::filesystem::current_path().string());
    std::string vendor_out = cfire::bridge::run_command_capture(vendor_cmd, rc);
    require(rc == 0, std::format("cfire vendor exit 0: {}", vendor_out));
    require(vendor_out.contains("Successfully vendored 1 package(s)"), "vendor reports 1 package vendored");
    require(std::filesystem::exists(app_dir / "vendor" / "Calc" / "calc.h"), "vendored calc.h exists");
    require(std::filesystem::exists(app_dir / "vendor" / "Calc" / "calc.c"), "vendored calc.c exists");

    // 3. Delete remote repo and local cache to verify offline build works strictly from vendor/
    std::filesystem::remove_all(remote_repo);
    std::filesystem::remove_all(app_dir / ".cfire" / "cache");

    std::string offline_out = cfire::bridge::run_command_capture(run_cmd, rc);
    require(rc == 0, std::format("offline cfire run exit 0: {}", offline_out));
    require(offline_out.contains("sum=40"), std::format("offline output contains sum=40: {}", offline_out));

    std::println("--- PASS: test_cfire_remote_vendor");
}



void test_in_process_sha256_file_hashing() {
    std::println("=== RUN   test_in_process_sha256_file_hashing");
    ScopedTempDir tmp("cfire_sha_test");
    auto empty_file = tmp.path / "empty.txt";
    (void)cfire::fs::write_text(empty_file, "");
    auto h_empty = cfire::sha256::hash_file_hex(empty_file);
    require(h_empty.has_value(), "hash_file_hex on empty file succeeds");
    require(*h_empty == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        std::format("empty file SHA-256 mismatch: {}", *h_empty));

    auto hello_file = tmp.path / "hello.txt";
    (void)cfire::fs::write_text(hello_file, "hello world");
    auto h_hello = cfire::sha256::hash_file_hex(hello_file);
    require(h_hello.has_value(), "hash_file_hex on hello world succeeds");
    require(*h_hello == "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9",
        std::format("hello world SHA-256 mismatch: {}", *h_hello));

    auto nonexistent = tmp.path / "does_not_exist.txt";
    auto h_missing = cfire::sha256::hash_file_hex(nonexistent);
    require(!h_missing.has_value(), "hash_file_hex on missing file returns error");

    std::println("--- PASS: test_in_process_sha256_file_hashing");
}



void test_crypto_hmac_and_aes(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_crypto_hmac_and_aes");
    (void)rt_dir;
    // 1. In-process C++ unit tests
    // hmac::equal
    require(cfire::hmac::equal("abcdef123456", "abcdef123456"), "hmac::equal identical strings");
    require(!cfire::hmac::equal("abcdef123456", "abcdef123457"), "hmac::equal different strings");
    require(!cfire::hmac::equal("abcdef123456", "abcdef"), "hmac::equal different lengths");

    std::string key_str = "01234567890123456789012345678901"; // 32 bytes
    std::string iv_str  = "1234567890123456";                 // 16 bytes
    std::string pt_str  = "Hello Systems Programming in Cfire!";

    // AES-256 CTR
    auto ct_res = cfire::aes::encrypt_ctr(key_str, iv_str, pt_str);
    require(ct_res.has_value(), "aes::encrypt_ctr success");
    auto dec_ctr = cfire::aes::decrypt_ctr(key_str, iv_str, *ct_res);
    require(dec_ctr.has_value(), "aes::decrypt_ctr success");
    std::string dec_ctr_str(reinterpret_cast<const char*>(dec_ctr->data()), dec_ctr->size());
    require(dec_ctr_str == pt_str, "aes::decrypt_ctr roundtrip match");

    // AES-256 GCM
    std::string nonce_str = "123456789012"; // 12 bytes
    std::string aad_str = "auth_data_v1";
    auto gcm_res = cfire::aes::encrypt_gcm(key_str, nonce_str, pt_str, aad_str);
    require(gcm_res.has_value(), "aes::encrypt_gcm success");
    require(gcm_res->tag.size() == 16, "aes::encrypt_gcm tag size 16");

    auto dec_gcm_str = cfire::aes::decrypt_gcm_str(key_str, nonce_str, gcm_res->ciphertext, gcm_res->tag, aad_str);
    require(dec_gcm_str.has_value(), "aes::decrypt_gcm_str success");
    require(*dec_gcm_str == pt_str, "aes::decrypt_gcm_str roundtrip match");

    // GCM Tampering detection
    auto tampered_tag = gcm_res->tag;
    tampered_tag[0] = static_cast<std::byte>(static_cast<unsigned char>(tampered_tag[0]) ^ 0x01);
    auto tamper_res = cfire::aes::decrypt_gcm_str(key_str, nonce_str, gcm_res->ciphertext, tampered_tag, aad_str);
    require(!tamper_res.has_value(), "aes::decrypt_gcm rejects corrupted tag");

    auto tampered_ct = gcm_res->ciphertext;
    tampered_ct[0] = static_cast<std::byte>(static_cast<unsigned char>(tampered_ct[0]) ^ 0x01);
    auto tamper_ct_res = cfire::aes::decrypt_gcm_str(key_str, nonce_str, tampered_ct, gcm_res->tag, aad_str);
    require(!tamper_ct_res.has_value(), "aes::decrypt_gcm rejects corrupted ciphertext");

    auto tamper_aad_res = cfire::aes::decrypt_gcm_str(key_str, nonce_str, gcm_res->ciphertext, gcm_res->tag, "wrong_aad");
    require(!tamper_aad_res.has_value(), "aes::decrypt_gcm rejects mismatched AAD");

    // 2. End-to-end cfire run invocation
    run_cfire_source(R"(package main

import "core:crypto/aes"
import "core:crypto/hmac"

fn main() {
    key := "01234567890123456789012345678901"
    nonce := "012345678901"
    msg := "cfire aes-gcm authenticated encryption secret"
    aad := "meta-header"

    res := try aes.encrypt_gcm(key, nonce, msg, aad)
    dec := try aes.decrypt_gcm_str(key, nonce, res.ciphertext, res.tag, aad)
    println("decrypted={}", dec)

    eq1 := hmac.equal("hash123456", "hash123456")
    eq2 := hmac.equal("hash123456", "hash999999")
    println("eq1={} eq2={}", eq1, eq2)
}
)", "cfire_crypto")
        .assert_ok("cfire run crypto main.cf exit 0")
        .assert_contains("decrypted=cfire aes-gcm authenticated encryption secret", "output contains decrypted secret")
        .assert_contains("eq1=true eq2=false", "output contains eq1=true eq2=false");

    std::println("--- PASS: test_crypto_hmac_and_aes");
}



void test_sync_context([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_sync_context");
    // 1. C++ runtime assertions
    auto bg = cfire::sync::context::background();
    require(!bg.is_cancelled(), "background context is not cancelled");
    require(!bg.done(), "background context done() is false");

    // with_cancel manual cancellation
    auto [child1, cancel1] = cfire::sync::context::with_cancel(bg);
    require(!child1.is_cancelled(), "child context not cancelled initially");
    cancel1("request aborted");
    require(child1.is_cancelled(), "child context cancelled after cancel()");
    require(child1.reason() == "request aborted", "child context records cancellation reason");
    require(!bg.is_cancelled(), "parent background context remains uncancelled");

    // Parent cancellation cascading to multiple children
    auto [parent_ctx, parent_cancel] = cfire::sync::context::with_cancel(bg);
    auto [sub_child1, _c1] = cfire::sync::context::with_cancel(parent_ctx);
    auto [sub_child2, _c2] = cfire::sync::context::with_cancel(parent_ctx);
    require(!sub_child1.is_cancelled() && !sub_child2.is_cancelled(), "sub-children active");
    parent_cancel("parent terminated");
    require(sub_child1.is_cancelled(), "sub_child1 cancelled via cascade");
    require(sub_child2.is_cancelled(), "sub_child2 cancelled via cascade");
    require(sub_child1.reason() == "parent terminated", "sub_child1 inherited parent reason");

    // with_timeout automatic expiration
    auto [timeout_ctx, timeout_cancel] = cfire::sync::context::with_timeout(bg, 50);
    require(!timeout_ctx.is_cancelled(), "timeout context active initially");
    bool timeout_w = timeout_ctx.wait_for(150);
    require(timeout_w, "timeout_ctx wait_for unblocks on deadline expiration");
    require(timeout_ctx.is_cancelled(), "timeout_ctx is cancelled after expiration");
    require(timeout_ctx.reason() == "deadline exceeded", "timeout_ctx reason is deadline exceeded");

    // sleep_ctx interrupted by cancellation
    auto [sleep_ctx_handle, sleep_cancel] = cfire::sync::context::with_cancel(bg);
    std::thread cancel_thread([sleep_cancel]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        sleep_cancel("interrupted");
    });
    auto t0 = std::chrono::steady_clock::now();
    bool sleep_res = cfire::time::sleep_ctx(sleep_ctx_handle, 1000);
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    cancel_thread.join();
    require(!sleep_res, "sleep_ctx returns false when cancelled early");
    require(elapsed_ms < 500, "sleep_ctx unblocks promptly upon cancellation");

    // Channel.recv_ctx unblocks immediately on cancellation
    cfire::sync::Channel<int> ch;
    auto [ch_ctx, ch_cancel] = cfire::sync::context::with_cancel(bg);
    std::atomic<bool> unblocked = false;
    std::string err_msg;
    std::thread recv_thread([&ch, &ch_ctx, &unblocked, &err_msg]() {
        auto res = ch.recv_ctx(ch_ctx);
        unblocked = true;
        if (!res) err_msg = std::string(res.error().message());
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    require(!unblocked, "receiver blocks on empty channel before cancellation");
    ch_cancel("client disconnected");
    recv_thread.join();
    require(unblocked, "receiver unblocks promptly when context cancelled");
    require(err_msg.contains("context cancelled: client disconnected"), "recv_ctx returns error with cancellation reason");

    // 2. End-to-end .cf script execution
    run_cfire_source(R"(package main

import "core:sync/context"
import "core:sync"
import "core:time"

fn main() {
    bg := context.background()
    (ctx1, cancel1) := context.with_cancel(bg)
    println("ctx1.done={}", ctx1.done())
    cancel1("stop")
    println("ctx1.cancelled={}", ctx1.is_cancelled())
    println("ctx1.reason={}", ctx1.reason())

    (tctx, tcancel) := context.with_timeout(bg, 30)
    time.sleep_ms(60)
    println("tctx.done={}", tctx.done())
    println("tctx.reason={}", tctx.reason())
}
)", "cfire_ctx")
        .assert_ok("cfire run context exit 0")
        .assert_contains("ctx1.done=false", "ctx1 initial state")
        .assert_contains("ctx1.cancelled=true", "ctx1 cancelled state")
        .assert_contains("ctx1.reason=stop", "ctx1 reason output")
        .assert_contains("tctx.done=true", "tctx expired state")
        .assert_contains("tctx.reason=deadline exceeded", "tctx reason output");

    std::println("--- PASS: test_sync_context");
}



void test_net_websocket([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_net_websocket");
    // 1. C++ runtime assertions
    // RFC 6455 Handshake vector test:
    std::string rfc_key = "dGhlIHNhbXBsZSBub25jZQ==";
    std::string accept = cfire::websocket::compute_accept_key(rfc_key);
    require(accept == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "RFC 6455 Sec-WebSocket-Accept test vector match");

    // Loopback pair test
    auto pair_res = cfire::websocket::pair();
    require(pair_res.has_value(), "cfire::websocket::pair() succeeds");
    auto [client, server] = std::move(*pair_res);
    require(client.is_open() && server.is_open(), "both sockets open");

    // Text frame client -> server
    auto send_txt = client.send_text("Hello RFC 6455 WebSockets!");
    require(send_txt.has_value(), "client.send_text succeeds");
    auto recv_txt = server.recv_text();
    require(recv_txt.has_value(), "server.recv_text succeeds");
    require(*recv_txt == "Hello RFC 6455 WebSockets!", "text frame content matches exactly");

    // Text frame server -> client
    auto reply_txt = server.send_text("Server Ack");
    require(reply_txt.has_value(), "server.send_text succeeds");
    auto got_reply = client.recv_text();
    require(got_reply.has_value() && *got_reply == "Server Ack", "client receives unmasked server text frame");

    // Binary frame client -> server
    std::vector<std::uint8_t> bin_data = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
    auto send_bin = client.send_binary(bin_data);
    require(send_bin.has_value(), "client.send_binary succeeds");
    auto recv_bin = server.recv_binary();
    require(recv_bin.has_value() && *recv_bin == bin_data, "server receives binary frame matching payload");

    // Ping / Pong handling
    auto ping_res = client.ping("ping-heartbeat");
    require(ping_res.has_value(), "client.ping succeeds");
    auto srv_post_ping = server.send_text("alive");
    require(srv_post_ping.has_value(), "server sends alive");
    auto cl_got_alive = client.recv_text();
    require(cl_got_alive.has_value() && *cl_got_alive == "alive", "client receives text after ping");

    // Clean close
    client.close(1000, "done");
    require(!client.is_open(), "client closed");
    auto srv_recv_closed = server.recv_text();
    require(!srv_recv_closed.has_value(), "server recognizes closed peer");

    // 2. End-to-end .cf script execution
    run_cfire_source(R"(package main

import "core:net/websocket"

fn main() {
    (mut client, mut server) := try websocket.pair()
    try client.send_text("cf-websocket-hello")
    msg := try server.recv_text()
    println("ws.server.msg={}", msg)

    try server.send_text("cf-websocket-ack")
    ack := try client.recv_text()
    println("ws.client.ack={}", ack)
    client.close()
}
)", "cfire_ws")
        .assert_ok("cfire run websocket exit 0")
        .assert_contains("ws.server.msg=cf-websocket-hello", "websocket server receive output")
        .assert_contains("ws.client.ack=cf-websocket-ack", "websocket client ack output");

    std::println("--- PASS: test_net_websocket");
}



void test_net_http_and_router(const std::filesystem::path& /*rt_dir*/) {
    std::println("=== RUN   test_net_http_and_router");

    // 1. Request Parser Testing
    std::string raw_get = "GET /api/users/42?sort=desc&limit=10 HTTP/1.1\r\n"
                          "Host: 127.0.0.1:8080\r\n"
                          "User-Agent: test-client/1.0\r\n"
                          "Accept: application/json\r\n\r\n";
    auto req_res = cfire::http::parse_request(raw_get);
    require(req_res.has_value(), "parse_request succeeds for valid GET");
    if (req_res.has_value()) {
        const auto& req = *req_res;
        require(req.method == "GET", "parsed method is GET");
        require(req.path == "/api/users/42", "parsed path is /api/users/42");
        require(req.raw_query == "sort=desc&limit=10", "parsed raw query");
        require(req.query("sort") == "desc", "parsed query param sort");
        require(req.query("limit") == "10", "parsed query param limit");
        require(req.query("missing", "default") == "default", "query fallback value");
        require(req.header("host") == "127.0.0.1:8080", "header lookup lower-case");
        require(req.header("Host") == "127.0.0.1:8080", "header lookup case-insensitive");
        require(req.header("Accept") == "application/json", "header lookup Accept");
    }

    std::string raw_post = "POST /submit HTTP/1.1\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: 15\r\n\r\n"
                           "{\"status\":\"ok\"}";
    auto post_res = cfire::http::parse_request(raw_post);
    require(post_res.has_value(), "parse_request succeeds for valid POST with body");
    if (post_res.has_value()) {
        require(post_res->method == "POST", "parsed method is POST");
        require(post_res->path == "/submit", "parsed path is /submit");
        require(post_res->body == "{\"status\":\"ok\"}", "parsed body matches Content-Length");
    }

    auto bad_res = cfire::http::parse_request("invalid without crlfcrlf");
    require(!bad_res.has_value(), "parse_request returns error for malformed request");

    // 2. Response & Wire Formatting Testing
    auto resp_txt = cfire::http::Response::text(200, "hello world");
    require(resp_txt.status_code == 200, "Response::text status code 200");
    require(resp_txt.header("content-type").contains("text/plain"), "Response::text content-type");
    require(resp_txt.body == "hello world", "Response::text body");

    auto resp_json = cfire::http::Response::json(201, "{\"id\":99}");
    require(resp_json.status_code == 201, "Response::json status code 201");
    require(resp_json.header("content-type") == "application/json", "Response::json content-type");

    auto resp_wire = resp_json.to_http_wire();
    require(resp_wire.starts_with("HTTP/1.1 201 Created\r\n"), "to_http_wire status line");
    require(resp_wire.contains("Content-Length: 9\r\n"), "to_http_wire Content-Length");
    require(resp_wire.ends_with("{\"id\":99}"), "to_http_wire payload suffix");

    // 3. Independent Router Testing (pure in-memory dispatch, zero sockets)
    auto router = cfire::http::make_router();

    router.get("/health", [](const cfire::http::Request&) {
        return cfire::http::Response::json(200, "{\"healthy\":true}");
    });

    router.get("/users/:id", [](const cfire::http::Request& req) {
        return cfire::http::Response::text(200, "user_id=" + req.param("id"));
    });

    router.get("/orgs/:org/repos/:repo", [](const cfire::http::Request& req) {
        return cfire::http::Response::text(200, req.param("org") + "/" + req.param("repo"));
    });

    router.get("/static/*filepath", [](const cfire::http::Request& req) {
        return cfire::http::Response::text(200, "file=" + req.param("filepath"));
    });

    router.post("/items", [](const cfire::http::Request& req) {
        return cfire::http::Response::json(201, "created:" + req.body);
    });

    // Exact match
    cfire::http::Request r1{.method = "GET", .path = "/health"};
    auto out1 = router.dispatch(r1);
    require(out1.status_code == 200 && out1.body == "{\"healthy\":true}", "router exact match /health");

    // Parameter match :id
    cfire::http::Request r2{.method = "GET", .path = "/users/456"};
    auto out2 = router.dispatch(r2);
    require(out2.status_code == 200 && out2.body == "user_id=456", "router parameter match :id");

    // Multi-parameter match :org and :repo
    cfire::http::Request r3{.method = "GET", .path = "/orgs/google/repos/cfire"};
    auto out3 = router.dispatch(r3);
    require(out3.status_code == 200 && out3.body == "google/cfire", "router multi-param match :org/:repo");

    // Wildcard match *filepath
    cfire::http::Request r4{.method = "GET", .path = "/static/assets/css/main.css"};
    auto out4 = router.dispatch(r4);
    require(out4.status_code == 200 && out4.body == "file=assets/css/main.css", "router wildcard match *filepath");

    // POST dispatch
    cfire::http::Request r5{.method = "POST", .path = "/items", .body = "gadget"};
    auto out5 = router.dispatch(r5);
    require(out5.status_code == 201 && out5.body == "created:gadget", "router POST dispatch with body");

    // 405 Method Not Allowed (path matches POST /items but method is GET)
    cfire::http::Request r6{.method = "GET", .path = "/items"};
    auto out6 = router.dispatch(r6);
    require(out6.status_code == 405, "router returns 405 Method Not Allowed for unmatched verb");
    require(out6.header("allow") == "POST", "router sets Allow header for 405 response");

    // 404 Not Found
    cfire::http::Request r7{.method = "GET", .path = "/nonexistent"};
    auto out7 = router.dispatch(r7);
    require(out7.status_code == 404, "router returns 404 for unmatched path");

    // Middleware test
    auto mw_router = cfire::http::make_router();
    mw_router.use([](cfire::http::Request& req) -> std::optional<cfire::http::Response> {
        if (req.header("authorization") != "Bearer secret") {
            return cfire::http::Response::text(401, "Unauthorized");
        }
        return std::nullopt;
    });
    mw_router.get("/protected", [](const cfire::http::Request&) {
        return cfire::http::Response::text(200, "SecretData");
    });

    cfire::http::Request unauth_req{.method = "GET", .path = "/protected"};
    require(mw_router.dispatch(unauth_req).status_code == 401, "middleware rejects missing auth");

    cfire::http::Request auth_req{.method = "GET", .path = "/protected", .headers = {{"authorization", "Bearer secret"}}};
    require(mw_router.dispatch(auth_req).status_code == 200, "middleware allows authenticated request");

    // 4. Server + Router Loopback End-to-End
    auto srv_res = cfire::http::make_server(0);
    require(srv_res.has_value(), "make_server creates listener on ephemeral port");
    if (srv_res.has_value()) {
        auto port = srv_res->port();
        srv_res->serve_in_background(router);

        std::string target_get = std::format("http://127.0.0.1:{}/users/888", port);
        auto client_get = cfire::http::get(target_get);
        require(client_get.has_value(), "client GET against live server with router succeeds");
        if (client_get.has_value()) {
            require(client_get->status_code == 200, "live GET status code 200");
            require(client_get->body == "user_id=888", "live GET received router output");
        }

        std::string target_post = std::format("http://127.0.0.1:{}/items", port);
        auto client_post = cfire::http::post(target_post, "gizmo");
        require(client_post.has_value(), "client POST against live server succeeds");
        if (client_post.has_value()) {
            require(client_post->status_code == 201, "live POST status code 201");
            require(client_post->body == "created:gizmo", "live POST received router output");
        }

        srv_res->stop();
    }

    // 5. End-to-end .cf script execution
    run_cfire_source(R"(package main

import "core:net/http"

fn main() {
    mut router := http.make_router()
    router.get("/ping", |req| {
        return http.Response.text(200, "pong")
    })
    router.get("/users/:id", |req| {
        return http.Response.text(200, "uid:" + req.param("id"))
    })

    // 1. In-memory standalone dispatch test
    req1 := http.Request{ method: "GET", path: "/ping" }
    res1 := router.dispatch(req1)
    println("cf.router.ping={}", res1.body)

    req2 := http.Request{ method: "GET", path: "/users/99" }
    res2 := router.dispatch(req2)
    println("cf.router.user={}", res2.body)
}
)", "cfire_http_router")
        .assert_ok("cfire run http router exit 0")
        .assert_contains("cf.router.ping=pong", "http router ping dispatch output")
        .assert_contains("cf.router.user=uid:99", "http router user parameter dispatch output");

    std::println("--- PASS: test_net_http_and_router");
}


