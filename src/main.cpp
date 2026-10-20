// src/main.cpp — Native C++23 CLI Driver for cfire (.cf)
// Milestone 11: Self-hosted via pure cfire (src/main.cf) with CFIRE_CXX_MAIN=1 fallback.

#include "main.hpp"

#if defined(CFIRE_CXX_MAIN)
namespace {
using namespace cfire::main_driver;

auto legacy_cxx_main(int argc, char** argv) -> int {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    std::string sub = argv[1];
    auto runtime_dir = locate_runtime_dir();

    if (sub == "lsp") {
        return run_lsp();
    }

    if (sub == "ide") {
        std::string editor = (argc >= 3) ? argv[2] : "auto";
        return run_ide(editor, runtime_dir.string());
    }

    if (sub == "eval") {
        bool expr_mode = false;
        std::string raw_code;
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "-e") {
                expr_mode = true;
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    raw_code = argv[++i];
                }
            } else if (raw_code.empty()) {
                raw_code = arg;
            } else {
                raw_code += " ";
                raw_code += arg;
            }
        }

        if (raw_code.empty() || raw_code == "-") {
            std::string line;
            raw_code.clear();
            while (std::getline(std::cin, line)) {
                raw_code += line;
                raw_code += "\n";
            }
        }

        if (raw_code.empty()) {
            std::println(stderr, "error: no code provided to eval");
            return 1;
        }

        return run_eval(raw_code, expr_mode, runtime_dir.string());
    }

    if (sub == "refactor") {
        if (argc < 3) {
            std::println(stderr, "Usage: cfire refactor rename <file>:<line>:<col> <new_name> [--diff|--apply|--dry-run|--json]");
            std::println(stderr, "   or: cfire refactor rename --file <file> --line <l> --col <c> --to <new_name> [--diff|--apply|--dry-run|--json]");
            return 1;
        }
        std::string action = argv[2];
        if (action != "rename") {
            std::println(stderr, "unknown refactor action '{}' (supported: rename)", action);
            return 1;
        }

        std::filesystem::path target_file;
        int target_line = 0;
        int target_col = 0;
        std::string new_name;
        bool apply_mode = false;
        bool diff_mode = false;
        bool dry_run_mode = false;
        bool json_mode = false;

        for (int i = 3; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--apply") {
                apply_mode = true;
            } else if (arg == "--diff") {
                diff_mode = true;
            } else if (arg == "--dry-run") {
                dry_run_mode = true;
            } else if (arg == "--json") {
                json_mode = true;
            } else if (arg == "--file" && i + 1 < argc) {
                target_file = argv[++i];
            } else if (arg == "--line" && i + 1 < argc) {
                target_line = std::atoi(argv[++i]);
            } else if (arg == "--col" && i + 1 < argc) {
                target_col = std::atoi(argv[++i]);
            } else if (arg == "--to" && i + 1 < argc) {
                new_name = argv[++i];
            } else if (target_file.empty() && arg.find(':') != std::string_view::npos) {
                auto first_colon = arg.find(':');
                auto second_colon = arg.find(':', first_colon + 1);
                target_file = arg.substr(0, first_colon);
                if (second_colon != std::string_view::npos) {
                    target_line = std::atoi(std::string(arg.substr(first_colon + 1, second_colon - first_colon - 1)).c_str());
                    target_col = std::atoi(std::string(arg.substr(second_colon + 1)).c_str());
                } else {
                    target_line = std::atoi(std::string(arg.substr(first_colon + 1)).c_str());
                    target_col = 1;
                }
            } else if (new_name.empty()) {
                new_name = arg;
            }
        }

        if (target_file.empty() || target_line <= 0 || new_name.empty()) {
            std::println(stderr, "error: missing required arguments for refactor rename");
            std::println(stderr, "Usage: cfire refactor rename <file>:<line>:<col> <new_name> [--diff|--apply|--dry-run|--json]");
            return 1;
        }

        if (target_col <= 0) target_col = 1;

        return run_refactor(target_file.string(), target_line, target_col, new_name, apply_mode, diff_mode, dry_run_mode, json_mode, true, "");
    }

    if (sub == "vendor") {
        std::string target_dir = (argc >= 3) ? argv[2] : "vendor";
        return run_vendor(target_dir);
    }

    if (sub == "doc") {
        bool check_only = false;
        bool llm_mode = false;
        std::string target_cf;
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
        return run_doc(check_only, llm_mode, target_cf, runtime_dir.string(), selfhost_get_llm_doc());
    }

    if (sub == "fmt") {
        bool check_only = false;
        bool to_stdout = false;
        std::string target_path = ".";
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
        return run_fmt(target_path, check_only, to_stdout);
    }

    if (sub == "outline") {
        std::string target_path = ".";
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg != "--json") {
                target_path = arg;
            }
        }
        return run_outline(target_path);
    }

    if (argc < 3) {
        print_usage();
        return 1;
    }
    bool sanitize = false;
    bool json_mode = false;
    std::string target_triple;
    std::string sysroot;
    std::string target;
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
        if ((arg == "--target" || arg == "-target") && i + 1 < argc) {
            target_triple = argv[++i];
            continue;
        }
        if (arg.starts_with("--target=")) {
            target_triple = arg.substr(9);
            continue;
        }
        if (arg.starts_with("-target=")) {
            target_triple = arg.substr(8);
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
        if (target.empty()) {
            target = arg;
        } else {
            run_args.emplace_back(arg);
        }
    }
    if (target.empty()) {
        print_usage();
        return 1;
    }

    if (sub == "emit-cpp") {
        return run_emit_cpp(target, runtime_dir.string(), target_triple, sysroot);
    }

    if (sub == "check") {
        return run_check(target, runtime_dir.string(), json_mode, target_triple, sysroot);
    }

    if (sub == "build") {
        return run_build(target, runtime_dir.string(), sanitize, target_triple, sysroot);
    }

    if (sub == "run" || sub == "test") {
        return run_test(target, runtime_dir.string(), (sub == "test"), sanitize, target_triple, sysroot, run_args);
    }

    if (sub == "eject") {
        std::string out_dir = (argc >= 4) ? argv[3] : "ejected_cpp";
        return run_eject(target, runtime_dir.string(), out_dir);
    }

    print_usage();
    return 1;
}

} // namespace
#endif

auto main(int argc, char** argv) -> int {
#if !defined(CFIRE_CXX_MAIN)
    std::vector<std::string> args;
    args.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    auto runtime_dir = cfire::main_driver::locate_runtime_dir().string();
    return selfhost_run_driver(args, runtime_dir);
#else
    return legacy_cxx_main(argc, argv);
#endif
}
