// src/main.cpp — Native C++23 CLI Driver for cfire (.cf)

#include "cli/scaffold.hpp"
#include "cli/cmd_inspect_outline.hpp"
#include "cli/cmd_check_fix.hpp"

using namespace cfire::cli;

auto main(int argc, char** argv) -> int {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    std::string sub = argv[1];
    auto toolchain_layout = cfire::build::locate_toolchain_layout(argv[0]);
    auto runtime_dir = toolchain_layout.runtime_dir;

    if (sub == "--version" || sub == "-v" || sub == "version") {
        std::println("cfire {} ({}, ISO C++23 systems toolchain)", cfire::doctor::kCfireVersion, cfire::doctor::kCfireGitSha);
        return 0;
    }

    if (sub == "--help" || sub == "-h" || sub == "help") {
        print_usage();
        return 0;
    }

    if (sub == "init" || sub == "new") {
        std::string tmpl = "cli";
        bool force = false;
        std::filesystem::path target_dir;
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if ((arg == "--template" || arg == "-t") && i + 1 < argc) {
                tmpl = argv[++i];
            } else if (arg.starts_with("--template=")) {
                tmpl = std::string(arg.substr(11));
            } else if (arg == "--tako") {
                tmpl = "tako";
            } else if (arg == "--gui" || arg == "--raylib") {
                tmpl = "gui";
            } else if (arg == "--webui") {
                tmpl = "webui";
            } else if (arg == "--cli") {
                tmpl = "cli";
            } else if (arg == "--force" || arg == "-f") {
                force = true;
            } else if (!arg.starts_with("-") && target_dir.empty()) {
                target_dir = arg;
            }
        }
        if (sub == "new" && target_dir.empty()) {
            std::println(stderr, "error: `cfire new` requires a target directory (e.g., `cfire new my_app`)");
            return 1;
        }
        if (target_dir.empty()) target_dir = ".";
        return run_scaffold_command(sub, target_dir, tmpl, force);
    }

    if (sub == "install") {
        bool portable_mode = false;
        bool user_mode = false;
        std::string target_dir;
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--portable" || arg == "--dir" || arg == "--bundle") {
                portable_mode = true;
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    target_dir = argv[++i];
                }
            } else if (arg == "--prefix" && i + 1 < argc) {
                target_dir = argv[++i];
            } else if (arg.starts_with("--prefix=")) {
                target_dir = std::string(arg.substr(9));
            } else if (arg == "--user") {
                user_mode = true;
            } else if (!arg.starts_with("-") && target_dir.empty()) {
                target_dir = std::string(arg);
            }
        }
        return cfire::doctor::run_install(toolchain_layout, target_dir, portable_mode, user_mode);
    }

    if (sub == "doctor") {
        bool json_mode = false;
        for (int i = 2; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--json") json_mode = true;
        }
        return cfire::doctor::run_doctor(runtime_dir, json_mode);
    }

    if (sub == "setup") {
        bool auto_yes = false;
        bool with_wasm = false;
        bool with_raylib = false;
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--yes" || arg == "-y") auto_yes = true;
            else if (arg == "--wasm" || arg == "--web") with_wasm = true;
            else if (arg == "--raylib" || arg == "--gui") with_raylib = true;
        }
        return cfire::doctor::run_setup(runtime_dir, auto_yes, with_wasm, with_raylib);
    }

    if (sub == "serve" || sub == "server") {
        int port = 8080;
        bool open_browser = true;
        std::filesystem::path target_path = ".";
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--no-open") {
                open_browser = false;
                continue;
            }
            if (arg == "--open") {
                open_browser = true;
                continue;
            }
            if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
                try { port = std::stoi(argv[++i]); } catch (...) {}
                continue;
            }
            if (arg.starts_with("--port=")) {
                try { port = std::stoi(std::string(arg.substr(7))); } catch (...) {}
                continue;
            }
            if (arg.starts_with("-p=")) {
                try { port = std::stoi(std::string(arg.substr(3))); } catch (...) {}
                continue;
            }
            target_path = arg;
        }

        std::string explicit_entry;
        auto cur_dir = std::filesystem::current_path();
        std::string target_name;
        if (target_path != "." && !std::filesystem::exists(target_path)) {
            target_name = target_path.string();
            target_path = ".";
        }

        auto bcf_res = cfire::build::resolve_build_target(cur_dir, target_name, true);
        if (bcf_res && bcf_res->has_wasm_target) {
            if (!std::filesystem::exists(bcf_res->wasm_html)) {
                std::println("==> Building {} (WebAssembly target from build.cf)...", bcf_res->exe.name);
                std::error_code ec;
                std::filesystem::create_directories(bcf_res->wasm_html.parent_path(), ec);
                auto plan = cfire::build::build_native(
                    bcf_res->entry_file,
                    runtime_dir,
                    bcf_res->wasm_html,
                    false,
                    false,
                    "wasm32-unknown-emscripten",
                    "",
                    {},
                    {},
                    bcf_res->exe.web_profile.empty() ? (bcf_res->exe.subsystem == "gui" ? "canvas" : "auto") : bcf_res->exe.web_profile,
                    bcf_res->exe.subsystem.empty() ? "auto" : bcf_res->exe.subsystem,
                    bcf_res->exe.app_name,
                    bcf_res->exe.bundle_id,
                    bcf_res->exe.icon
                );
                if (!plan) {
                    std::println(stderr, "error auto-building wasm target: {}", plan.error());
                    return 1;
                }
                std::println("✓ Built WebAssembly: {} (profile={})", bcf_res->wasm_html.string(), plan->web_profile);
            }
            target_path = bcf_res->wasm_html.parent_path();
            explicit_entry = bcf_res->wasm_html.filename().string();
        }

        return cfire::serve::run_server(target_path, port, open_browser, explicit_entry);
    }

    if (sub == "lsp") {
        cfire::lsp::Server srv;
        srv.handle_stream(std::cin, std::cout);
        return 0;
    }

    if (sub == "mcp") {
        bool setup_mode = false;
        bool print_cfg = false;
        bool global_scope = false;
        std::string target = "auto";
        std::filesystem::path ws_dir = std::filesystem::current_path();
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--setup" || arg == "setup" || arg == "--config" || arg == "config") {
                setup_mode = true;
            } else if (arg == "--print-config" || arg == "print-config") {
                print_cfg = true;
            } else if (arg == "--global" || arg == "-g") {
                global_scope = true;
            } else if (arg == "--dir" && i + 1 < argc) {
                ws_dir = argv[++i];
            } else if (arg == "--all" || arg == "all" || !arg.starts_with("-")) {
                target = std::string(arg);
            }
        }
        if (print_cfg) {
            std::print("{}", cfire::lsp::format_mcp_config_snippet(runtime_dir.parent_path()));
            return 0;
        }
        if (setup_mode) {
            auto res = cfire::lsp::configure_ide(ws_dir, target, global_scope);
            if (!res) {
                std::println(stderr, "{}", res.error());
                return 1;
            }
            std::print("{}", *res);
            return 0;
        }
        return cfire::mcp::serve_stdio(runtime_dir);
    }

    if (sub == "ide") {
        std::string editor = "auto";
        bool global_scope = false;
        std::filesystem::path ws_dir = std::filesystem::current_path();
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--global" || arg == "-g") {
                global_scope = true;
            } else if (arg == "--dir" && i + 1 < argc) {
                ws_dir = argv[++i];
            } else {
                editor = std::string(arg);
            }
        }
        auto res = cfire::lsp::configure_ide(ws_dir, editor, global_scope);
        if (!res) {
            std::println(stderr, "{}", res.error());
            return 1;
        }
        std::print("{}", *res);
        return 0;
    }

    if (sub == "eval") return run_eval_subcommand(argc, argv, runtime_dir);
    if (sub == "refactor") return run_refactor_subcommand(argc, argv);
    if (sub == "vendor") return run_vendor_subcommand(argc, argv);

    if (sub == "doc") {
        bool check_only = false;
        bool llm_mode = false;
        std::filesystem::path target_cf;
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--check") {
                check_only = true;
            } else if (arg == "--llm") {
                llm_mode = true;
            } else {
                target_cf = arg;
            }
        }
        if (llm_mode) {
            std::println("{}", cfire::doc::generate_llm_reference());
            return 0;
        }
        if (!target_cf.empty() && target_cf.extension() == ".cf") {
            auto md = cfire::doc::extract_file_markdown_doc(target_cf);
            if (!md) {
                std::println(stderr, "{}", md.error());
                return 1;
            }
            std::print("{}", *md);
            return 0;
        }
        auto docs_dir = toolchain_layout.docs_dir;
        auto res = cfire::doc::sync_docs_directory(docs_dir, check_only, runtime_dir);
        if (!res) {
            std::println(stderr, "{}", res.error());
            return 1;
        }
        if (check_only) {
            std::println("✓ Documentation in {} is 100% up-to-date with the compiler", docs_dir.string());
        } else {
            std::println("✓ Generated landing page (index.html), guide (guide.html, GUIDE.md), and stdlib reference (reference.html, STDLIB_REFERENCE.md) in {}", docs_dir.string());
        }
        return 0;
    }

    if (sub == "fmt") {
        bool check_only = false;
        bool to_stdout = false;
        std::filesystem::path target_path = ".";
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--check") {
                check_only = true;
            } else if (arg == "--stdout") {
                to_stdout = true;
            } else {
                target_path = arg;
            }
        }
        auto res = cfire::fmt::format_path(target_path, check_only, to_stdout);
        if (!res) {
            std::println(stderr, "{}", res.error());
            return 1;
        }
        if (check_only && *res > 0) {
            std::println(stderr, "✗ {} file(s) need formatting (run `cfire fmt {}`)", *res, target_path.string());
            return 1;
        }
        if (!to_stdout) {
            std::println("✓ cfire fmt: {} ({} file(s) updated)", target_path.string(), *res);
        }
        return 0;
    }

    if (sub == "outline") return run_outline_subcommand(argc, argv);

    if (argc < 3) {
        if (sub != "build" && sub != "run" && sub != "test" && sub != "bench" && sub != "check" && sub != "fix") {
            print_usage();
            return 1;
        }
    }
    bool sanitize = false;
    bool json_mode = false;
    bool fix_mode = false;
    bool dry_run = false;
    bool bench_mode = (sub == "bench");
    std::string filter_pattern;
    std::string fuzz_runs_arg;
    std::string fuzz_seed_arg;
    std::string bench_time_arg;
    std::string custom_output_path;
    std::string target_triple;
    std::string sysroot;
    std::string web_profile = "auto";
    std::string subsystem = "auto";
    std::string app_name_override;
    std::string bundle_id_override;
    std::string icon_path_override;
    std::string ios_device_override;
    std::string build_target_filter;
    int web_port = 8080;
    bool open_browser = true;
    std::vector<std::string> extra_link_libs;
    std::vector<std::string> extra_includes;
    std::filesystem::path target;
    std::vector<std::string> run_args;
    bool after_dashdash = false;
    for (int i = 2; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (after_dashdash) {
            run_args.emplace_back(arg);
            continue;
        }
        if (arg == "--") {
            after_dashdash = true;
            continue;
        }
        if (arg == "--sanitize") {
            sanitize = true;
            continue;
        }
        if (arg == "--json") {
            json_mode = true;
            continue;
        }
        if (arg == "--fix") {
            fix_mode = true;
            continue;
        }
        if (arg == "--dry-run") {
            dry_run = true;
            continue;
        }
        if (arg == "--bench" || arg == "-bench") {
            bench_mode = true;
            continue;
        }
        if ((arg == "--fuzz-runs" || arg == "-fuzz-runs") && i + 1 < argc) {
            fuzz_runs_arg = argv[++i];
            continue;
        }
        if (arg.starts_with("--fuzz-runs=")) {
            fuzz_runs_arg = std::string(arg.substr(12));
            continue;
        }
        if ((arg == "--seed" || arg == "-seed") && i + 1 < argc) {
            fuzz_seed_arg = argv[++i];
            continue;
        }
        if (arg.starts_with("--seed=")) {
            fuzz_seed_arg = std::string(arg.substr(7));
            continue;
        }
        if ((arg == "--bench-time" || arg == "-bench-time") && i + 1 < argc) {
            bench_time_arg = argv[++i];
            continue;
        }
        if (arg.starts_with("--bench-time=")) {
            bench_time_arg = std::string(arg.substr(13));
            continue;
        }
        if ((arg == "--filter" || arg == "-filter") && i + 1 < argc) {
            filter_pattern = argv[++i];
            continue;
        }
        if (arg.starts_with("--filter=")) {
            filter_pattern = arg.substr(9);
            continue;
        }
        if (arg.starts_with("-filter=")) {
            filter_pattern = arg.substr(8);
            continue;
        }
        if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            custom_output_path = argv[++i];
            continue;
        }
        if (arg.starts_with("--output=")) {
            custom_output_path = arg.substr(9);
            continue;
        }
        if (arg.starts_with("-o=")) {
            custom_output_path = arg.substr(3);
            continue;
        }
        if ((arg == "--icon" || arg == "-icon") && i + 1 < argc) {
            icon_path_override = argv[++i];
            continue;
        }
        if (arg.starts_with("--icon=")) {
            icon_path_override = arg.substr(7);
            continue;
        }
        if ((arg == "--name" || arg == "--app-name") && i + 1 < argc) {
            app_name_override = argv[++i];
            continue;
        }
        if (arg.starts_with("--name=")) {
            app_name_override = arg.substr(7);
            continue;
        }
        if (arg.starts_with("--app-name=")) {
            app_name_override = arg.substr(11);
            continue;
        }
        if ((arg == "--bundle-id" || arg == "-bundle-id") && i + 1 < argc) {
            bundle_id_override = argv[++i];
            continue;
        }
        if (arg.starts_with("--bundle-id=")) {
            bundle_id_override = arg.substr(12);
            continue;
        }
        if (arg == "--target=wasm" || arg == "-target=wasm" || arg == "--target=web" || arg == "-target=web" || arg == "--web" || arg == "-web") {
            target_triple = "wasm32-unknown-emscripten";
            continue;
        }
        if (arg == "--ios" || arg == "-ios" || arg == "--ios-sim" || arg == "-ios-sim" ||
            arg == "--target=ios-sim" || arg == "-target=ios-sim") {
            target_triple = "ios-sim";
            continue;
        }
        if (arg == "--target=ios" || arg == "-target=ios") {
            target_triple = "ios";
            continue;
        }
        if ((arg == "--device" || arg == "-device") && i + 1 < argc) {
            ios_device_override = argv[++i];
            continue;
        }
        if (arg.starts_with("--device=")) {
            ios_device_override = arg.substr(9);
            continue;
        }
        if (arg.starts_with("--web-profile=")) {
            web_profile = arg.substr(14);
            continue;
        }
        if (arg.starts_with("-web-profile=")) {
            web_profile = arg.substr(13);
            continue;
        }
        if (arg.starts_with("--subsystem=")) {
            subsystem = arg.substr(12);
            continue;
        }
        if (arg.starts_with("-subsystem=")) {
            subsystem = arg.substr(11);
            continue;
        }
        if (arg == "--no-open") {
            open_browser = false;
            continue;
        }
        if (arg == "--open") {
            open_browser = true;
            continue;
        }
        if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            try { web_port = std::stoi(argv[++i]); } catch (...) {}
            continue;
        }
        if (arg.starts_with("--port=")) {
            try { web_port = std::stoi(std::string(arg.substr(7))); } catch (...) {}
            continue;
        }
        if (arg.starts_with("-p=")) {
            try { web_port = std::stoi(std::string(arg.substr(3))); } catch (...) {}
            continue;
        }
        if ((arg == "--target" || arg == "-target") && i + 1 < argc) {
            target_triple = argv[++i];
            if (target_triple == "wasm" || target_triple == "web") {
                target_triple = "wasm32-unknown-emscripten";
            }
            continue;
        }
        if (arg.starts_with("--target=")) {
            target_triple = arg.substr(9);
            if (target_triple == "wasm" || target_triple == "web") {
                target_triple = "wasm32-unknown-emscripten";
            }
            continue;
        }
        if (arg.starts_with("-target=")) {
            target_triple = arg.substr(8);
            if (target_triple == "wasm" || target_triple == "web") {
                target_triple = "wasm32-unknown-emscripten";
            }
            continue;
        }
        if (arg == "--sysroot" && i + 1 < argc) {
            sysroot = argv[++i];
            continue;
        }
        if (arg.starts_with("--sysroot=")) {
            sysroot = arg.substr(10);
            continue;
        }
        if (arg == "--link-lib" && i + 1 < argc) {
            extra_link_libs.emplace_back(argv[++i]);
            continue;
        }
        if (arg.starts_with("--link-lib=")) {
            extra_link_libs.emplace_back(arg.substr(11));
            continue;
        }
        if (arg == "--include" && i + 1 < argc) {
            extra_includes.emplace_back(argv[++i]);
            continue;
        }
        if (arg.starts_with("--include=")) {
            extra_includes.emplace_back(arg.substr(10));
            continue;
        }
        if (target.empty()) {
            target = arg;
        } else {
            run_args.emplace_back(arg);
        }
    }

    if (sub == "build" || sub == "run") {
        auto build_cf_cur = std::filesystem::current_path() / "build.cf";
        if (target.empty() || target == "." || target == "all") {
            if (std::filesystem::exists(build_cf_cur)) {
                if (target == "all") build_target_filter = "all";
                target = build_cf_cur;
            }
        } else if (target != "build.cf" && target.extension() != ".cf") {
            if (std::filesystem::exists(build_cf_cur)) {
                build_target_filter = target.string();
                target = build_cf_cur;
            }
        }
        if (sub == "build" && target.filename() == "build.cf" && build_target_filter.empty() && !run_args.empty()) {
            build_target_filter = run_args[0];
        }
    } else if (sub == "test" || sub == "bench" || sub == "check" || sub == "fix") {
        if (target.empty()) {
            target = ".";
        }
    }

    if (target.empty()) {
        print_usage();
        return 1;
    }

    if (sub == "fix" || (sub == "check" && fix_mode)) {
        int fix_rc = run_fix_subcommand(sub, target, json_mode, dry_run);
        if (sub == "fix") return fix_rc;
    }

    if (sub == "inspect") {
        return run_inspect_subcommand(target, runtime_dir, run_args, extra_includes, json_mode);
    }

    if (sub == "emit-cpp") {
        auto res = cfire::build::analyze_and_emit(target, runtime_dir, false, false, target_triple, sysroot);
        if (!res) {
            std::println(stderr, "{}", res.error());
            return 1;
        }
        std::print("{}", res->first);
        return 0;
    }

    if (sub == "check") {
        return run_check_subcommand(target, runtime_dir, json_mode, target_triple, sysroot, extra_includes, web_profile, subsystem);
    }

    if (sub == "build") {
        if (target.filename() == "build.cf") {
            auto parsed = cfire::build::parse_build_cf(target);
            if (!parsed) {
                std::println(stderr, "error parsing build.cf: {}", parsed.error());
                return 1;
            }
            if (parsed->executables.empty()) {
                std::println(stderr, "error: no executables declared in {}", target.string());
                return 1;
            }

            bool build_all = build_target_filter.empty() || build_target_filter == "all";
            bool matched_any = false;
            bool has_var_name_match = false;
            if (!build_all) {
                for (const auto& exe : parsed->executables) {
                    if (exe.var_name == build_target_filter) {
                        has_var_name_match = true;
                        break;
                    }
                }
            }

            for (const auto& exe : parsed->executables) {
                if (!build_all) {
                    bool matches = has_var_name_match
                        ? (exe.var_name == build_target_filter)
                        : (exe.name == build_target_filter || exe.app_name == build_target_filter);
                    if (!matches) continue;
                }
                matched_any = true;

                std::vector<std::string> active_targets = exe.targets;
                if (!target_triple.empty()) {
                    active_targets = { target_triple };
                }

                std::filesystem::path entry_file = exe.entry;
                if (entry_file.is_relative()) {
                    entry_file = target.parent_path() / entry_file;
                }

                for (const auto& tgt : active_targets) {
                    bool is_wasm_target = (tgt == "wasm" || tgt == "web" || tgt.starts_with("wasm32"));
                    std::string effective_triple = is_wasm_target ? "wasm32-unknown-emscripten" : (tgt == "native" ? "" : tgt);
                    std::string effective_profile = is_wasm_target ? (!web_profile.empty() && web_profile != "auto" ? web_profile : (exe.web_profile.empty() ? (exe.subsystem == "gui" ? "canvas" : "auto") : exe.web_profile)) : web_profile;
                    std::string effective_subsystem = !subsystem.empty() && subsystem != "auto" ? subsystem : (exe.subsystem.empty() ? "auto" : exe.subsystem);
                    std::string effective_app_name = !app_name_override.empty() ? app_name_override : exe.app_name;
                    std::string effective_bundle_id = !bundle_id_override.empty() ? bundle_id_override : exe.bundle_id;
                    std::string effective_icon = !icon_path_override.empty() ? icon_path_override : exe.icon;

                    std::filesystem::path out_bin_path;
                    if (!custom_output_path.empty()) {
                        out_bin_path = custom_output_path;
                    } else {
                        std::filesystem::path base_out_dir = exe.out_dir.empty() ? target.parent_path() : (target.parent_path() / exe.out_dir);
                        if (is_wasm_target) {
                            out_bin_path = base_out_dir / (exe.name + ".html");
                        } else {
                            std::error_code ec;
                            auto candidate = base_out_dir / exe.name;
                            if (std::filesystem::is_directory(candidate, ec)) {
                                out_bin_path = candidate / exe.name;
                            } else {
                                out_bin_path = candidate;
                            }
                        }
                    }

                    std::error_code ec;
                    std::filesystem::create_directories(out_bin_path.parent_path(), ec);

                    auto plan = cfire::build::build_native(
                        entry_file,
                        runtime_dir,
                        out_bin_path,
                        false,
                        sanitize,
                        effective_triple,
                        sysroot,
                        extra_link_libs,
                        extra_includes,
                        effective_profile,
                        effective_subsystem,
                        effective_app_name,
                        effective_bundle_id,
                        effective_icon
                    );
                    if (!plan) {
                        std::println(stderr, "error building target '{}' for executable '{}':\n{}",
                            tgt, exe.name, plan.error());
                        return 1;
                    }

                    if (is_wasm_target) {
                        std::println("✓ Built WebAssembly: {} (profile={})", out_bin_path.string(), plan->web_profile);
                    } else if (plan->is_ios) {
                        std::println("✓ Built iOS App Bundle: {} (target={})", plan->ios_app_bundle, plan->target_triple);
                    } else {
                        std::string bundle_info = plan->macos_app_bundle.empty() ? "" : std::format(" -> {}", plan->macos_app_bundle);
                        std::println("✓ Built Native {}: {}{}",
                            plan->subsystem == "gui" ? "App" : "Binary",
                            out_bin_path.string(),
                            bundle_info);
                    }
                }
            }

            if (!matched_any) {
                std::println(stderr, "error: executable '{}' not found in {}", build_target_filter, target.string());
                return 1;
            }
            return 0;
        }

        std::string out_bin;
        if (!custom_output_path.empty()) {
            out_bin = custom_output_path;
        } else {
            out_bin = target.parent_path().filename().string();
            if (out_bin.empty() || out_bin == ".") out_bin = "cfire_app";
            std::error_code ec;
            if (std::filesystem::is_directory(out_bin, ec)) {
                out_bin = (std::filesystem::path(out_bin) / target.stem().string()).string();
            }
        }
        if (!target_triple.empty() && (target_triple.find("windows") != std::string::npos || target_triple.find("win32") != std::string::npos || target_triple.find("msvc") != std::string::npos)) {
            if (!out_bin.ends_with(".exe")) out_bin += ".exe";
        }
        if (target_triple.starts_with("wasm")) {
            if (!out_bin.ends_with(".html") && !out_bin.ends_with(".js")) {
                out_bin += ".html";
            }
        }
        auto plan = cfire::build::build_native(
            target,
            runtime_dir,
            out_bin,
            false,
            sanitize,
            target_triple,
            sysroot,
            extra_link_libs,
            extra_includes,
            web_profile,
            subsystem,
            app_name_override,
            bundle_id_override,
            icon_path_override
        );
        if (!plan) {
            std::println(stderr, "{}", plan.error());
            return 1;
        }
        std::string target_info = plan->target_triple.empty() ? "" : std::format(", target={}", plan->target_triple);
        std::println("✓ Built {} (generated {}, pch={}, sanitize={}{})",
            out_bin, plan->generated_cpp, plan->used_pch, plan->sanitized, target_info);
        if (!plan->macos_app_bundle.empty()) {
            std::println("✓ Created macOS App Bundle: {}", plan->macos_app_bundle);
        }
        if (!plan->ios_app_bundle.empty()) {
            std::println("✓ Created iOS App Bundle: {}", plan->ios_app_bundle);
        }
        return 0;
    }

    if (sub == "run" || sub == "test" || sub == "bench") {
        bool is_test = (sub == "test" || sub == "bench");
        if (is_test) {
            if (json_mode) {
                run_args.emplace_back("--cfire-test-json");
            }
            if (bench_mode) {
                run_args.emplace_back("--cfire-bench");
            }
            if (!filter_pattern.empty()) {
                run_args.emplace_back(std::format("--cfire-test-filter={}", filter_pattern));
            }
            if (!fuzz_runs_arg.empty()) {
                run_args.emplace_back(std::format("--cfire-fuzz-runs={}", fuzz_runs_arg));
            }
            if (!fuzz_seed_arg.empty()) {
                run_args.emplace_back(std::format("--cfire-seed={}", fuzz_seed_arg));
            }
            if (!bench_time_arg.empty()) {
                run_args.emplace_back(std::format("--cfire-bench-time={}", bench_time_arg));
            }

            std::error_code test_ec;
            if (std::filesystem::is_directory(target, test_ec)) {
                // Discover all test suites in directory (ERG-01a), grouped by (directory, package_name)
                std::vector<std::filesystem::path> all_cf;
                for (const auto& entry : std::filesystem::recursive_directory_iterator(target, test_ec)) {
                    if (!entry.is_regular_file() || entry.path().extension() != ".cf") continue;
                    if (entry.path().filename() == "build.cf") continue;
                    if (is_ignored_walk_dir(entry.path())) continue;
                    all_cf.push_back(entry.path());
                }
                std::sort(all_cf.begin(), all_cf.end());

                // Prefer `*_test.cf` files first within each directory so they act as the primary suite entry
                std::stable_sort(all_cf.begin(), all_cf.end(), [](const auto& a, const auto& b) {
                    bool a_test = a.filename().string().ends_with("_test.cf");
                    bool b_test = b.filename().string().ends_with("_test.cf");
                    if (a.parent_path() == b.parent_path() && a_test != b_test) {
                        return a_test > b_test;
                    }
                    return false;
                });

                std::unordered_set<std::string> seen_pkg_suites;
                std::vector<std::filesystem::path> suite_entries;
                for (const auto& fpath : all_cf) {
                    auto raw = cfire::build::read_file_text(fpath);
                    if (!raw) continue;
                    auto ast = cfire::frontend::parse_source(fpath.string(), *raw);
                    if (!ast) continue;
                    cfire::frontend::append_doctests(fpath.string(), *raw, *ast);
                    bool is_test_name = fpath.filename().string().ends_with("_test.cf");
                    if (!is_test_name && ast->tests.empty()) continue;

                    std::string pkg_key = std::filesystem::weakly_canonical(fpath.parent_path(), test_ec).string() + "::" + ast->package_name;
                    if (!ast->package_name.empty()) {
                        if (seen_pkg_suites.contains(pkg_key)) continue;
                        seen_pkg_suites.insert(pkg_key);
                    }
                    suite_entries.push_back(fpath);
                }

                if (suite_entries.empty()) {
                    if (json_mode) {
                        std::println("{{\"success\": true, \"passed\": 0, \"failed\": 0, \"tests\": []}}");
                    } else {
                        std::println("✓ No test suites found in {}", target.string());
                    }
                    return 0;
                }

                int total_passed = 0;
                int total_failed = 0;
                int suite_failures = 0;
                std::string combined_tests_json;

                for (std::size_t idx = 0; idx < suite_entries.size(); ++idx) {
                    const auto& suite_file = suite_entries[idx];
                    if (!json_mode && suite_entries.size() > 1) {
                        std::println("==> Suite {}/{}: {}", idx + 1, suite_entries.size(), suite_file.string());
                    }
#if defined(_WIN32)
                    auto tmp_bin = std::filesystem::temp_directory_path() / std::format("cfire-test-{}-{}.exe", ::getpid(), idx);
#else
                    auto tmp_bin = std::filesystem::temp_directory_path() / std::format("cfire-test-{}-{}", ::getpid(), idx);
#endif
                    auto plan = cfire::build::build_native(suite_file, runtime_dir, tmp_bin, true, sanitize, target_triple, sysroot, extra_link_libs, extra_includes, web_profile, subsystem);
                    if (!plan) {
                        ++suite_failures;
                        if (!json_mode) {
                            std::println(stderr, "{}", plan.error());
                        }
                        continue;
                    }
                    std::string cmd = cfire::bridge::shell_quote(tmp_bin.string());
                    for (const auto& arg : run_args) {
                        cmd += " " + cfire::bridge::shell_quote(arg);
                    }
                    if (json_mode) {
                        int rc = 0;
                        std::string out = cfire::bridge::run_command_capture(cmd, rc);
                        std::filesystem::remove(tmp_bin, test_ec);
                        if (rc != 0) ++suite_failures;
                        // Extract passed, failed, and tests array from suite JSON
                        if (auto p_pos = out.find("\"passed\": "); p_pos != std::string::npos) {
                            p_pos += 10;
                            auto p_end = out.find_first_of(",}", p_pos);
                            if (auto v = cfire::detail::parse_int<int>(std::string_view(out).substr(p_pos, p_end - p_pos))) {
                                total_passed += *v;
                            }
                        }
                        if (auto f_pos = out.find("\"failed\": "); f_pos != std::string::npos) {
                            f_pos += 10;
                            auto f_end = out.find_first_of(",}", f_pos);
                            if (auto v = cfire::detail::parse_int<int>(std::string_view(out).substr(f_pos, f_end - f_pos))) {
                                total_failed += *v;
                            }
                        }
                        if (auto t_pos = out.find("\"tests\": ["); t_pos != std::string::npos) {
                            t_pos += 10;
                            auto t_end = out.rfind("]}");
                            if (t_end != std::string::npos && t_end > t_pos) {
                                std::string inner = out.substr(t_pos, t_end - t_pos);
                                if (!inner.empty()) {
                                    if (!combined_tests_json.empty()) combined_tests_json += ", ";
                                    combined_tests_json += inner;
                                }
                            }
                        }
                    } else {
                        std::string sys_cmd = cfire::bridge::win32_quote_cmd(cmd);
                        int rc = std::system(sys_cmd.c_str());
                        std::filesystem::remove(tmp_bin, test_ec);
                        if (rc != 0) ++suite_failures;
                    }
                }

                if (json_mode) {
                    bool ok = (suite_failures == 0 && total_failed == 0);
                    std::println("{{\"success\": {}, \"passed\": {}, \"failed\": {}, \"tests\": [{}]}}",
                        ok ? "true" : "false", total_passed, total_failed, combined_tests_json);
                } else if (suite_entries.size() > 1) {
                    if (suite_failures == 0) {
                        std::println("✓ All {} test suite(s) passed", suite_entries.size());
                    } else {
                        std::println(stderr, "✗ {} of {} test suite(s) failed", suite_failures, suite_entries.size());
                    }
                }
                return suite_failures > 0 ? 1 : 0;
            }
        }

        if (!is_test && target.filename() == "build.cf") {
            bool is_wasm_run = target_triple.starts_with("wasm");
            bool is_ios_run = cfire::build::is_ios_target(target_triple);
            auto bcf_res = cfire::build::resolve_build_target(target, build_target_filter, is_wasm_run);
            if (!bcf_res) {
                std::println(stderr, "error: no matching executable found in {}", target.string());
                return 1;
            }

            if (is_ios_run) {
                std::string run_triple = (target_triple == "ios" || target_triple == "ios-sim") ? "ios-sim" : target_triple;
                auto ios_bin = bcf_res->base_out_dir / "ios-sim" / bcf_res->exe.name;
                std::error_code ec;
                std::filesystem::create_directories(ios_bin.parent_path(), ec);
                std::println("==> Building {} (iOS Simulator target from build.cf)...", bcf_res->exe.name);
                auto plan = cfire::build::build_native(
                    bcf_res->entry_file,
                    runtime_dir,
                    ios_bin,
                    false,
                    false,
                    run_triple,
                    sysroot,
                    extra_link_libs,
                    extra_includes,
                    bcf_res->exe.web_profile,
                    bcf_res->exe.subsystem,
                    !app_name_override.empty() ? app_name_override : bcf_res->exe.app_name,
                    !bundle_id_override.empty() ? bundle_id_override : bcf_res->exe.bundle_id,
                    !icon_path_override.empty() ? icon_path_override : bcf_res->exe.icon
                );
                if (!plan) {
                    std::println(stderr, "error building iOS target: {}", plan.error());
                    return 1;
                }
                std::string bid = !bundle_id_override.empty() ? bundle_id_override
                    : (!bcf_res->exe.bundle_id.empty() ? bcf_res->exe.bundle_id : ("com.cfire." + bcf_res->exe.name));
                auto sim_res = cfire::build::launch_ios_simulator(plan->ios_app_bundle, bid, ios_device_override, run_args);
                if (!sim_res) {
                    std::println(stderr, "{}", sim_res.error());
                    return 1;
                }
                return *sim_res;
            }

            if (is_wasm_run) {
                if (!std::filesystem::exists(bcf_res->wasm_html)) {
                    std::println("==> Building {} (WebAssembly target from build.cf)...", bcf_res->exe.name);
                    std::error_code ec;
                    std::filesystem::create_directories(bcf_res->wasm_html.parent_path(), ec);
                    auto plan = cfire::build::build_native(
                        bcf_res->entry_file,
                        runtime_dir,
                        bcf_res->wasm_html,
                        false,
                        false,
                        "wasm32-unknown-emscripten",
                        "",
                        {},
                        {},
                        bcf_res->exe.web_profile.empty() ? (bcf_res->exe.subsystem == "gui" ? "canvas" : "auto") : bcf_res->exe.web_profile,
                        bcf_res->exe.subsystem.empty() ? "auto" : bcf_res->exe.subsystem,
                        bcf_res->exe.app_name,
                        bcf_res->exe.bundle_id,
                        bcf_res->exe.icon
                    );
                    if (!plan) {
                        std::println(stderr, "error auto-building wasm target: {}", plan.error());
                        return 1;
                    }
                    std::println("✓ Built WebAssembly: {} (profile={})", bcf_res->wasm_html.string(), plan->web_profile);
                }
                return cfire::serve::run_server(bcf_res->wasm_html.parent_path(), web_port, open_browser, bcf_res->wasm_html.filename().string());
            }

            // Native run
            [[maybe_unused]] bool is_gui = (bcf_res->exe.subsystem == "gui");
            bool needs_build = false;
#if defined(__APPLE__)
            if (is_gui) {
                if (!std::filesystem::exists(bcf_res->macos_app_bundle)) {
                    needs_build = true;
                }
            } else {
                if (!std::filesystem::exists(bcf_res->native_bin)) {
                    needs_build = true;
                }
            }
#else
            if (!std::filesystem::exists(bcf_res->native_bin)) {
                needs_build = true;
            }
#endif

            if (needs_build) {
                std::println("==> Building {} (Native target from build.cf)...", bcf_res->exe.name);
                std::error_code ec;
                std::filesystem::create_directories(bcf_res->native_bin.parent_path(), ec);
                auto plan = cfire::build::build_native(
                    bcf_res->entry_file,
                    runtime_dir,
                    bcf_res->native_bin,
                    false,
                    sanitize,
                    target_triple,
                    sysroot,
                    extra_link_libs,
                    extra_includes,
                    bcf_res->exe.web_profile,
                    bcf_res->exe.subsystem,
                    bcf_res->exe.app_name,
                    bcf_res->exe.bundle_id,
                    bcf_res->exe.icon
                );
                if (!plan) {
                    std::println(stderr, "error building native target: {}", plan.error());
                    return 1;
                }
                std::string bundle_info = plan->macos_app_bundle.empty() ? "" : std::format(" -> {}", plan->macos_app_bundle);
                std::println("✓ Built Native {}: {}{}",
                    plan->subsystem == "gui" ? "App" : "Binary",
                    bcf_res->native_bin.string(),
                    bundle_info);
            }

#if defined(__APPLE__)
            if (is_gui && !bcf_res->macos_app_bundle.empty() && std::filesystem::exists(bcf_res->macos_app_bundle)) {
                std::println("==> Launching macOS App: {}", bcf_res->macos_app_bundle.string());
                std::string open_cmd = "open " + cfire::bridge::shell_quote(bcf_res->macos_app_bundle.string());
                int rc = std::system(open_cmd.c_str());
                return rc == 0 ? 0 : 1;
            }
#endif
            std::string cmd = cfire::bridge::shell_quote(bcf_res->native_bin.string());
            for (const auto& arg : run_args) {
                cmd += " " + cfire::bridge::shell_quote(arg);
            }
            std::string sys_cmd = cfire::bridge::win32_quote_cmd(cmd);
            int rc = std::system(sys_cmd.c_str());
            return unwrap_system_exit_code(rc);
        }

        if (target_triple.starts_with("wasm")) {
            auto tmp_html = std::filesystem::temp_directory_path() / std::format("cfire-wasm-{}.html", ::getpid());
            auto js_file = tmp_html;
            js_file.replace_extension(".js");
            auto wasm_file = tmp_html;
            wasm_file.replace_extension(".wasm");
            auto cleanup_wasm = [&]() {
                std::error_code ec;
                std::filesystem::remove(tmp_html, ec);
                std::filesystem::remove(js_file, ec);
                std::filesystem::remove(wasm_file, ec);
            };
            auto plan = cfire::build::build_native(target, runtime_dir, tmp_html, is_test, sanitize, target_triple, sysroot, extra_link_libs, extra_includes, web_profile, subsystem);
            if (!plan) {
                std::println(stderr, "{}", plan.error());
                return 1;
            }
            if (plan->web_profile == "cli") {
                int has_node = 0;
#if defined(_WIN32)
                (void)cfire::bridge::run_command_capture("where node 2>NUL", has_node);
#else
                (void)cfire::bridge::run_command_capture("which node 2>/dev/null", has_node);
#endif
                if (has_node == 0) {
                    std::string cmd = "node " + cfire::bridge::shell_quote(js_file.string());
                    for (const auto& arg : run_args) {
                        cmd += " " + cfire::bridge::shell_quote(arg);
                    }
                    std::string sys_cmd = cfire::bridge::win32_quote_cmd(cmd);
                    int rc = std::system(sys_cmd.c_str());
                    cleanup_wasm();
                    return unwrap_system_exit_code(rc);
                }
            }
            int srv_rc = cfire::serve::run_server(tmp_html, web_port, open_browser);
            cleanup_wasm();
            return srv_rc;
        }

        if (cfire::build::is_ios_target(target_triple)) {
            std::string run_triple = (target_triple == "ios" || target_triple == "ios-sim") ? "ios-sim" : target_triple;
            std::string app_stem = target.parent_path().filename().string();
            if (app_stem.empty() || app_stem == ".") app_stem = target.stem().string();
            if (app_stem == "main") app_stem = "cfire_ios_app";
            auto tmp_ios_dir = std::filesystem::temp_directory_path() / std::format("cfire-ios-{}", ::getpid());
            std::error_code ec;
            std::filesystem::create_directories(tmp_ios_dir, ec);
            auto tmp_ios_bin = tmp_ios_dir / app_stem;
            auto plan = cfire::build::build_native(
                target,
                runtime_dir,
                tmp_ios_bin,
                is_test,
                false,
                run_triple,
                sysroot,
                extra_link_libs,
                extra_includes,
                web_profile,
                subsystem,
                app_name_override,
                bundle_id_override,
                icon_path_override
            );
            if (!plan) {
                std::println(stderr, "{}", plan.error());
                return 1;
            }
            std::string bid = !bundle_id_override.empty() ? bundle_id_override : ("com.cfire." + app_stem);
            auto sim_res = cfire::build::launch_ios_simulator(plan->ios_app_bundle, bid, ios_device_override, run_args);
            std::filesystem::remove_all(tmp_ios_dir, ec);
            if (!sim_res) {
                std::println(stderr, "{}", sim_res.error());
                return 1;
            }
            return *sim_res;
        }

#if defined(_WIN32)
        auto tmp_bin = std::filesystem::temp_directory_path() / std::format("cfire-{}-{}.exe", sub, ::getpid());
#else
        auto tmp_bin = std::filesystem::temp_directory_path() / std::format("cfire-{}-{}", sub, ::getpid());
#endif
        auto plan = cfire::build::build_native(target, runtime_dir, tmp_bin, is_test, sanitize, target_triple, sysroot, extra_link_libs, extra_includes, web_profile, subsystem);
        if (!plan) {
            std::println(stderr, "{}", plan.error());
            return 1;
        }
        std::string cmd = cfire::bridge::shell_quote(tmp_bin.string());
        for (const auto& arg : run_args) {
            cmd += " " + cfire::bridge::shell_quote(arg);
        }
        std::string sys_cmd = cfire::bridge::win32_quote_cmd(cmd);
        int rc = std::system(sys_cmd.c_str());
        std::filesystem::remove(tmp_bin);
        return unwrap_system_exit_code(rc);
    }

    if (sub == "eject") {
        std::filesystem::path out_dir = (argc >= 4) ? argv[3] : "ejected_cpp";
        auto res = cfire::build::eject_cmake(target, runtime_dir, out_dir);
        if (!res) {
            std::println(stderr, "{}", res.error());
            return 1;
        }
        std::println("✓ Ejected standalone C++23 + CMakeLists.txt to {}", out_dir.string());
        return 0;
    }

    print_usage();
    return 1;
}
