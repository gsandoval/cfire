// src/tu_scaffold.cpp — Translation Unit for `init`, `new`, `eval`, `refactor`, `vendor`, `serve` (CF-198)

#include "cli/dispatch.hpp"
#include "cli/scaffold.hpp"
#include "cli/usage.hpp"
#include "server.hpp"

#include <filesystem>
#if __has_include(<print>)
#include <print>
#endif
#include <string>
#include <string_view>

namespace cfire::cli::tu {

auto run_dev_server_tu(const std::filesystem::path& root, int port, bool open_browser, const std::string& explicit_entry) -> int {
    return cfire::serve::run_server(root, port, open_browser, explicit_entry);
}

auto run_scaffold_tu(std::string_view sub, int argc, char** argv, const std::filesystem::path& runtime_dir) -> int {
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
            } else if (arg == "--gui") {
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
        return cfire::cli::run_scaffold_command(sub, target_dir, tmpl, force);
    }

    if (sub == "eval") return cfire::cli::run_eval_subcommand(argc, argv, runtime_dir);
    if (sub == "refactor") return cfire::cli::run_refactor_subcommand(argc, argv);
    if (sub == "vendor") return cfire::cli::run_vendor_subcommand(argc, argv);

    // sub == "serve" || sub == "server"
    cfire::build::set_build_profile(cfire::build::BuildProfile::Release);
    cfire::build::g_no_trap = false;
    cfire::build::g_emit_dsym = false;
    cfire::build::g_no_binary_cache = false;

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

} // namespace cfire::cli::tu
