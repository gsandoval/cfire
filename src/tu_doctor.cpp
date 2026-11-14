// src/tu_doctor.cpp — Translation Unit for `install`, `doctor`, `setup`, and `config` (CF-198, CF-199)

#include "cli/dispatch.hpp"
#include "cli/usage.hpp"
#include "doctor.hpp"

#include <cstdlib>
#include <filesystem>
#if __has_include(<print>)
#include <print>
#endif
#include <string>
#include <string_view>

namespace cfire::cli::tu_doctor_detail {

inline void print_config_usage() {
    std::println(stderr, "Usage: cfire config <list|get|set|unset> [key] [value]");
    std::println(stderr, "Supported keys: cxx, cc, emxx, ar, ld (or toolchains.<key>)");
}

inline auto run_config_subcommand(int argc, char** argv) -> int {
    std::string_view action = (argc >= 3) ? std::string_view(argv[2]) : std::string_view("list");
    bool json_mode = false;
    for (int i = 2; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--json") {
            json_mode = true;
        }
    }
    if (action == "--json") {
        action = "list";
    }

    if (action == "list") {
        auto cfg = cfire::build::load_user_config_map();
        if (json_mode) {
            std::string path_escaped = cfire::build::json_escape_str(cfire::build::config_file_path().string());
            std::string map_json = cfire::build::serialize_flat_string_map_json(cfg);
            std::println("{{\"config_path\": \"{}\", \"toolchains\": {}}}", path_escaped, map_json);
            return 0;
        }
        if (cfg.empty()) {
            std::println("(no custom toolchain entries set in {})", cfire::build::config_file_path().string());
            return 0;
        }
        for (const auto& [k, v] : cfg) {
            std::println("{}={}", k, v);
        }
        return 0;
    }

    if (action == "get") {
        if (argc < 4) {
            std::println(stderr, "error: `cfire config get` requires a key");
            print_config_usage();
            return 1;
        }
        std::string_view key = argv[3];
        if (!cfire::build::is_valid_config_key(key)) {
            std::println(stderr, "error: unknown config key '{}' (supported: cxx, cc, emxx, ar, ld)", key);
            return 1;
        }
        auto val = cfire::build::get_config_value(key);
        if (!val.has_value()) {
            std::println(stderr, "error: config key '{}' is not set", cfire::build::normalize_config_key(key));
            return 1;
        }
        std::println("{}", *val);
        return 0;
    }

    if (action == "set") {
        if (argc < 5) {
            std::println(stderr, "error: `cfire config set` requires <key> <value>");
            print_config_usage();
            return 1;
        }
        std::string_view key = argv[3];
        std::string value = argv[4];
        if (!cfire::build::is_valid_config_key(key)) {
            std::println(stderr, "error: unknown config key '{}' (supported: cxx, cc, emxx, ar, ld)", key);
            return 1;
        }
        if (value.empty()) {
            std::println(stderr, "error: value for config key '{}' cannot be empty (use `cfire config unset`)", key);
            return 1;
        }
        if (!cfire::build::set_config_value(key, value)) {
            std::println(stderr, "error: failed to write config file '{}'", cfire::build::config_file_path().string());
            return 1;
        }
        cfire::build::ProbeCache::instance().clear_memory_and_disk();
        std::println("✓ set {}={}", cfire::build::normalize_config_key(key), value);
        return 0;
    }

    if (action == "unset") {
        if (argc < 4) {
            std::println(stderr, "error: `cfire config unset` requires <key>");
            print_config_usage();
            return 1;
        }
        std::string_view key = argv[3];
        if (!cfire::build::is_valid_config_key(key)) {
            std::println(stderr, "error: unknown config key '{}' (supported: cxx, cc, emxx, ar, ld)", key);
            return 1;
        }
        if (!cfire::build::unset_config_value(key)) {
            std::println(stderr, "error: failed to write config file '{}'", cfire::build::config_file_path().string());
            return 1;
        }
        cfire::build::ProbeCache::instance().clear_memory_and_disk();
        std::println("✓ unset {}", cfire::build::normalize_config_key(key));
        return 0;
    }

    std::println(stderr, "error: unknown config subcommand '{}'", action);
    print_config_usage();
    return 1;
}

} // namespace cfire::cli::tu_doctor_detail

namespace cfire::cli::tu {

auto run_doctor_tu(std::string_view sub, int argc, char** argv, const cfire::build::ToolchainLayout& layout) -> int {
    if (sub == "config") {
        return cfire::cli::tu_doctor_detail::run_config_subcommand(argc, argv);
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
        return cfire::doctor::run_install(layout, target_dir, portable_mode, user_mode);
    }

    if (sub == "doctor") {
        bool json_mode = false;
        for (int i = 2; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--json") json_mode = true;
        }
        return cfire::doctor::run_doctor(layout.runtime_dir, json_mode);
    }

    // sub == "setup"
    bool auto_yes = false;
    bool with_wasm = false;
    for (int i = 2; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--yes" || arg == "-y") auto_yes = true;
        else if (arg == "--wasm" || arg == "--web") with_wasm = true;
        else if (arg == "--raylib" || arg == "--gui") {
            // CF-185: raylib is no longer bundled; say so instead of silently ignoring the flag.
            std::println(stderr, "error: '{}' was removed: cfire no longer bundles raylib (use 'ext:webui' for GUIs, or 'import cpp' with build.cf to use raylib as a C library)", arg);
            return 2;
        }
    }
    return cfire::doctor::run_setup(layout.runtime_dir, auto_yes, with_wasm);
}

} // namespace cfire::cli::tu
