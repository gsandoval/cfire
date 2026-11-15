#pragma once
// src/build/toolchain_config.hpp — Precedence-ordered toolchain resolution & user config management

#include "cache_paths.hpp"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#ifndef CFIRE_INFRA_INLINE
#ifdef CFIRE_SPLIT_TUS
#define CFIRE_INFRA_INLINE
#else
#define CFIRE_INFRA_INLINE inline
#endif
#endif

namespace cfire::build {

enum class ToolSource {
    Cli,
    Env,
    Config,
    Auto
};

inline std::string_view tool_source_name(ToolSource s) {
    switch (s) {
        case ToolSource::Cli: return "cli";
        case ToolSource::Env: return "env";
        case ToolSource::Config: return "config";
        case ToolSource::Auto: return "auto";
    }
    return "auto";
}

struct ToolResolution {
    std::string command;
    std::filesystem::path executable_path;
    ToolSource source = ToolSource::Auto;
};

struct ToolchainCliOverrides {
    std::string cxx;
    std::string cc;
    std::string emxx;
    std::string ar;
    std::string ld;
};

inline ToolchainCliOverrides& cli_toolchain_overrides() {
    static ToolchainCliOverrides instance;
    return instance;
}

CFIRE_INFRA_INLINE std::string normalize_config_key(std::string_view raw_key);

CFIRE_INFRA_INLINE bool is_valid_config_key(std::string_view raw_key);

CFIRE_INFRA_INLINE const std::vector<std::string_view>& supported_config_keys();

CFIRE_INFRA_INLINE std::map<std::string, std::string> load_user_config_map();

CFIRE_INFRA_INLINE bool save_user_config_map(const std::map<std::string, std::string>& m);

CFIRE_INFRA_INLINE std::optional<std::string> get_config_value(std::string_view raw_key);

CFIRE_INFRA_INLINE bool set_config_value(std::string_view raw_key, const std::string& value);

CFIRE_INFRA_INLINE bool unset_config_value(std::string_view raw_key);

CFIRE_INFRA_INLINE bool is_executable_file(const std::filesystem::path& p);

CFIRE_INFRA_INLINE std::filesystem::path probe_executable_candidate(const std::filesystem::path& candidate);

CFIRE_INFRA_INLINE std::filesystem::path find_executable_in_path(std::string_view name);

CFIRE_INFRA_INLINE std::filesystem::path resolve_tool_executable_path(std::string_view command);

CFIRE_INFRA_INLINE std::optional<ToolResolution> resolve_explicit_tool_override(
    const std::string& cli_val,
    const char* env_name,
    std::string_view cfg_key
);

CFIRE_INFRA_INLINE std::string quote_cmd_if_spaces(const std::filesystem::path& p);

[[nodiscard]] inline bool compiler_is_gcc_driver(std::string_view command) {
    while (!command.empty() && (command.front() == ' ' || command.front() == '\t')) command.remove_prefix(1);
    if (command.empty()) return false;
    std::string_view head = command;
    if (command.front() == '"' || command.front() == '\'') {
        char q = command.front();
        size_t endq = command.find(q, 1);
        head = (endq == std::string_view::npos) ? command.substr(1) : command.substr(1, endq - 1);
    } else if (size_t sp = command.find_first_of(" \t"); sp != std::string_view::npos) {
        head = command.substr(0, sp);
    }
    std::string fname = std::filesystem::path(head).filename().string();
    std::transform(fname.begin(), fname.end(), fname.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (fname.ends_with(".exe")) fname.resize(fname.size() - 4);
    if (fname.contains("clang")) return false;
    return fname == "gcc" || fname == "g++" ||
           fname.ends_with("-gcc") || fname.ends_with("-g++") ||
           fname.starts_with("gcc-") || fname.starts_with("g++-");
}

CFIRE_INFRA_INLINE std::string resolve_cross_cxx_compiler(std::string_view target_triple);

CFIRE_INFRA_INLINE ToolResolution resolve_cxx_compiler_detailed(std::string_view target_triple = "");

CFIRE_INFRA_INLINE std::string resolve_cxx_compiler(std::string_view target_triple = "");

CFIRE_INFRA_INLINE ToolResolution resolve_c_compiler_detailed(std::string_view target_triple = "");

CFIRE_INFRA_INLINE std::string resolve_c_compiler(std::string_view target_triple = "");

CFIRE_INFRA_INLINE ToolResolution resolve_emscripten_compiler_detailed();

CFIRE_INFRA_INLINE std::string resolve_emscripten_compiler_cfg();

CFIRE_INFRA_INLINE ToolResolution resolve_emscripten_c_compiler_detailed();

CFIRE_INFRA_INLINE std::string resolve_emscripten_c_compiler_cfg();

CFIRE_INFRA_INLINE ToolResolution resolve_archiver_detailed(std::string_view target_triple = "");

CFIRE_INFRA_INLINE std::string resolve_archiver(std::string_view target_triple = "");

CFIRE_INFRA_INLINE ToolResolution resolve_linker_pref_detailed();

CFIRE_INFRA_INLINE std::string resolve_linker_pref();

CFIRE_INFRA_INLINE std::string format_linker_flag_from_pref(std::string_view ld_pref);

CFIRE_INFRA_INLINE std::string compiler_binary_fingerprint(const std::string& command);

#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_TU_INFRA_IMPL)

CFIRE_INFRA_INLINE std::string normalize_config_key(std::string_view raw_key) {
    if (raw_key.starts_with("toolchains.")) {
        raw_key.remove_prefix(std::string_view("toolchains.").size());
    }
    return std::string(raw_key);
}

CFIRE_INFRA_INLINE bool is_valid_config_key(std::string_view raw_key) {
    std::string k = normalize_config_key(raw_key);
    return k == "cxx" || k == "cc" || k == "emxx" || k == "ar" || k == "ld";
}

CFIRE_INFRA_INLINE const std::vector<std::string_view>& supported_config_keys() {
    static const std::vector<std::string_view> kKeys = {
        "cxx", "cc", "emxx", "ar", "ld"
    };
    return kKeys;
}

CFIRE_INFRA_INLINE std::map<std::string, std::string> load_user_config_map() {
    std::string raw;
    std::map<std::string, std::string> out;
    auto cfg_path = config_file_path();
    if (!read_text_file(cfg_path, raw) || raw.empty()) {
        return out;
    }
    if (!parse_flat_string_map_json(raw, out)) {
        out.clear();
    }
    return out;
}

CFIRE_INFRA_INLINE bool save_user_config_map(const std::map<std::string, std::string>& m) {
    return atomic_write_text_file(
        config_file_path(),
        serialize_flat_string_map_json(m)
    );
}

CFIRE_INFRA_INLINE std::optional<std::string> get_config_value(std::string_view raw_key) {
    std::string key = normalize_config_key(raw_key);
    if (!is_valid_config_key(key)) return std::nullopt;
    auto m = load_user_config_map();
    auto it = m.find(key);
    if (it == m.end() || it->second.empty()) return std::nullopt;
    return it->second;
}

CFIRE_INFRA_INLINE bool set_config_value(std::string_view raw_key, const std::string& value) {
    std::string key = normalize_config_key(raw_key);
    if (!is_valid_config_key(key)) return false;
    auto m = load_user_config_map();
    if (value.empty()) {
        m.erase(key);
    } else {
        m[key] = value;
    }
    return save_user_config_map(m);
}

CFIRE_INFRA_INLINE bool unset_config_value(std::string_view raw_key) {
    std::string key = normalize_config_key(raw_key);
    if (!is_valid_config_key(key)) return false;
    auto m = load_user_config_map();
    m.erase(key);
    return save_user_config_map(m);
}

CFIRE_INFRA_INLINE bool is_executable_file(const std::filesystem::path& p) {
    if (p.empty()) return false;
    std::error_code ec;
    if (!std::filesystem::exists(p, ec) || ec) return false;
    if (std::filesystem::is_directory(p, ec) || ec) return false;
#if defined(_WIN32)
    return true;
#else
    return ::access(p.c_str(), X_OK) == 0;
#endif
}

CFIRE_INFRA_INLINE std::filesystem::path probe_executable_candidate(const std::filesystem::path& candidate) {
    if (is_executable_file(candidate)) return candidate;
#if defined(_WIN32)
    if (!candidate.has_extension()) {
        for (const char* ext : {".exe", ".cmd", ".bat"}) {
            auto with_ext = std::filesystem::path(candidate.string() + ext);
            if (is_executable_file(with_ext)) return with_ext;
        }
    }
#endif
    return {};
}

CFIRE_INFRA_INLINE std::filesystem::path find_executable_in_path(std::string_view name) {
    if (name.empty()) return {};
    std::filesystem::path direct(name);
    if (direct.is_absolute() ||
        name.find('/') != std::string_view::npos ||
        name.find('\\') != std::string_view::npos) {
        return probe_executable_candidate(direct);
    }

    const char* path_env = std::getenv("PATH");
    if (!path_env || !*path_env) return {};
#if defined(_WIN32)
    constexpr char kPathSep = ';';
#else
    constexpr char kPathSep = ':';
#endif
    std::string_view pv(path_env);
    size_t start = 0;
    while (start <= pv.size()) {
        size_t sep = pv.find(kPathSep, start);
        std::string_view dir_sv = (sep == std::string_view::npos)
            ? pv.substr(start)
            : pv.substr(start, sep - start);
        if (!dir_sv.empty()) {
            auto found = probe_executable_candidate(std::filesystem::path(dir_sv) / std::string(name));
            if (!found.empty()) return found;
        }
        if (sep == std::string_view::npos) break;
        start = sep + 1;
    }
    return {};
}

CFIRE_INFRA_INLINE std::filesystem::path resolve_tool_executable_path(std::string_view command) {
    while (!command.empty() && (command.front() == ' ' || command.front() == '\t')) {
        command.remove_prefix(1);
    }
    while (!command.empty() && (command.back() == ' ' || command.back() == '\t')) {
        command.remove_suffix(1);
    }
    if (command.empty()) return {};

    // First try full string as a path (handles paths with spaces).
    if (auto p = probe_executable_candidate(std::filesystem::path(command)); !p.empty()) {
        return p;
    }

    // Otherwise resolve the leading binary token (e.g. "ccache clang++" -> first token).
    size_t sp = command.find_first_of(" \t");
    std::string_view head = (sp == std::string_view::npos) ? command : command.substr(0, sp);
    if (head.size() >= 2 &&
        ((head.front() == '"' && head.back() == '"') ||
         (head.front() == '\'' && head.back() == '\''))) {
        head = head.substr(1, head.size() - 2);
    }
    return find_executable_in_path(head);
}

CFIRE_INFRA_INLINE std::optional<ToolResolution> resolve_explicit_tool_override(
    const std::string& cli_val,
    const char* env_name,
    std::string_view cfg_key
) {
    if (!cli_val.empty()) {
        return ToolResolution{cli_val, resolve_tool_executable_path(cli_val), ToolSource::Cli};
    }
    if (env_name && *env_name) {
        if (const char* ev = std::getenv(env_name); ev && *ev) {
            std::string val(ev);
            return ToolResolution{val, resolve_tool_executable_path(val), ToolSource::Env};
        }
    }
    if (auto cfg = get_config_value(cfg_key); cfg.has_value() && !cfg->empty()) {
        return ToolResolution{*cfg, resolve_tool_executable_path(*cfg), ToolSource::Config};
    }
    return std::nullopt;
}

CFIRE_INFRA_INLINE std::string quote_cmd_if_spaces(const std::filesystem::path& p) {
    std::string s = p.string();
    if (s.find(' ') != std::string::npos && !(s.size() >= 2 && s.front() == '"' && s.back() == '"')) {
        return "\"" + s + "\"";
    }
    return s;
}

CFIRE_INFRA_INLINE std::string resolve_cross_cxx_compiler(std::string_view target_triple) {
    if (target_triple.empty() || target_triple.starts_with("wasm") ||
        target_triple == "ios" || target_triple == "ios-sim" ||
        target_triple.find("apple-ios") != std::string_view::npos) {
        return {};
    }
    std::string triple(target_triple);
    for (const std::string& cand : {
             triple + "-clang++",
             triple + "-g++"}) {
        if (auto found = find_executable_in_path(cand); !found.empty()) {
            return quote_cmd_if_spaces(found);
        }
    }
    if (auto home = user_home_dir(); !home.empty()) {
        std::filesystem::path opt_dir = home / ".local" / "opt";
        std::error_code ec;
        if (std::filesystem::is_directory(opt_dir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(opt_dir, ec)) {
                if (!entry.is_directory(ec)) continue;
                for (const std::string& prefixed : {triple + "-clang++", triple + "-g++"}) {
                    if (auto found = probe_executable_candidate(entry.path() / "bin" / prefixed); !found.empty()) {
                        return quote_cmd_if_spaces(found);
                    }
                }
            }
        }
    }
    return {};
}

CFIRE_INFRA_INLINE ToolResolution resolve_cxx_compiler_detailed(std::string_view target_triple) {
    if (!cli_toolchain_overrides().cxx.empty()) {
        const auto& val = cli_toolchain_overrides().cxx;
        return ToolResolution{val, resolve_tool_executable_path(val), ToolSource::Cli};
    }
    if (const char* ev = std::getenv("CFIRE_CXX"); ev && *ev) {
        std::string val(ev);
        return ToolResolution{val, resolve_tool_executable_path(val), ToolSource::Env};
    }
    if (!target_triple.empty()) {
        if (auto cross = resolve_cross_cxx_compiler(target_triple); !cross.empty()) {
            return ToolResolution{cross, resolve_tool_executable_path(cross), ToolSource::Auto};
        }
    }
    if (auto cfg = get_config_value("cxx"); cfg.has_value() && !cfg->empty()) {
        return ToolResolution{*cfg, resolve_tool_executable_path(*cfg), ToolSource::Config};
    }
    static constexpr const char* kAutoCxxCandidates[] = {
        "clang++",
        "clang++-21", "clang++-20", "clang++-19", "clang++-18",
        "clang++-17", "clang++-16", "clang++-15",
        "g++"
    };
    for (const char* cand : kAutoCxxCandidates) {
        if (auto found = find_executable_in_path(cand); !found.empty()) {
            return ToolResolution{quote_cmd_if_spaces(found), found, ToolSource::Auto};
        }
    }
    return ToolResolution{"clang++", {}, ToolSource::Auto};
}

CFIRE_INFRA_INLINE std::string resolve_cxx_compiler(std::string_view target_triple) {
    return resolve_cxx_compiler_detailed(target_triple).command;
}

CFIRE_INFRA_INLINE ToolResolution resolve_c_compiler_detailed(std::string_view target_triple) {
    if (auto ov = resolve_explicit_tool_override(
            cli_toolchain_overrides().cc, "CFIRE_CC", "cc")) {
        return *ov;
    }
    if (!target_triple.empty() && !target_triple.starts_with("wasm") &&
        target_triple != "ios" && target_triple != "ios-sim" &&
        target_triple.find("apple-ios") == std::string_view::npos) {
        std::string triple(target_triple);
        for (const std::string& cand : {triple + "-clang", triple + "-gcc"}) {
            if (auto found = find_executable_in_path(cand); !found.empty()) {
                return ToolResolution{quote_cmd_if_spaces(found), found, ToolSource::Auto};
            }
        }
        if (auto home = user_home_dir(); !home.empty()) {
            std::filesystem::path opt_dir = home / ".local" / "opt";
            std::error_code ec;
            if (std::filesystem::is_directory(opt_dir, ec)) {
                for (const auto& entry : std::filesystem::directory_iterator(opt_dir, ec)) {
                    if (!entry.is_directory(ec)) continue;
                    for (const std::string& prefixed : {triple + "-clang", triple + "-gcc"}) {
                        if (auto found = probe_executable_candidate(entry.path() / "bin" / prefixed); !found.empty()) {
                            return ToolResolution{quote_cmd_if_spaces(found), found, ToolSource::Auto};
                        }
                    }
                }
            }
        }
    }
    // Check sibling C compiler next to resolved C++ compiler (e.g. llvm/bin/clang++ -> llvm/bin/clang)
    auto cxx = resolve_cxx_compiler_detailed(target_triple);
    if (!cxx.executable_path.empty()) {
        auto parent = cxx.executable_path.parent_path();
        std::string fname = cxx.executable_path.filename().string();
        if (auto pos = fname.rfind("clang++"); pos != std::string::npos) {
            std::string c_name = fname;
            c_name.replace(pos, 7, "clang");
            if (auto sib = probe_executable_candidate(parent / c_name); !sib.empty()) {
                return ToolResolution{quote_cmd_if_spaces(sib), sib, ToolSource::Auto};
            }
        } else if (auto gpos = fname.rfind("g++"); gpos != std::string::npos) {
            std::string c_name = fname;
            c_name.replace(gpos, 3, "gcc");
            if (auto sib = probe_executable_candidate(parent / c_name); !sib.empty()) {
                return ToolResolution{quote_cmd_if_spaces(sib), sib, ToolSource::Auto};
            }
        }
    }
    static constexpr const char* kAutoCcCandidates[] = {
        "clang",
        "clang-21", "clang-20", "clang-19", "clang-18",
        "clang-17", "clang-16", "clang-15",
        "gcc"
    };
    for (const char* cand : kAutoCcCandidates) {
        if (auto found = find_executable_in_path(cand); !found.empty()) {
            return ToolResolution{quote_cmd_if_spaces(found), found, ToolSource::Auto};
        }
    }
    return ToolResolution{"clang", {}, ToolSource::Auto};
}

CFIRE_INFRA_INLINE std::string resolve_c_compiler(std::string_view target_triple) {
    return resolve_c_compiler_detailed(target_triple).command;
}

CFIRE_INFRA_INLINE ToolResolution resolve_emscripten_compiler_detailed() {
    if (auto ov = resolve_explicit_tool_override(
            cli_toolchain_overrides().emxx, "CFIRE_EMXX", "emxx")) {
        return *ov;
    }
    if (auto found = find_executable_in_path("em++"); !found.empty()) {
        return ToolResolution{quote_cmd_if_spaces(found), found, ToolSource::Auto};
    }
    if (const char* emsdk = std::getenv("EMSDK"); emsdk && *emsdk) {
        auto p = std::filesystem::path(emsdk) / "upstream" / "emscripten" / "em++";
        if (auto found = probe_executable_candidate(p); !found.empty()) {
            return ToolResolution{quote_cmd_if_spaces(found), found, ToolSource::Auto};
        }
    }
    if (auto home = user_home_dir(); !home.empty()) {
        auto p = home / ".emsdk" / "upstream" / "emscripten" / "em++";
        if (auto found = probe_executable_candidate(p); !found.empty()) {
            return ToolResolution{quote_cmd_if_spaces(found), found, ToolSource::Auto};
        }
    }
    static constexpr const char* kEmPaths[] = {
        "/opt/homebrew/opt/emscripten/libexec/em++",
        "/usr/local/opt/emscripten/libexec/em++",
        "/usr/lib/emscripten/em++"
    };
    for (const char* cand : kEmPaths) {
        if (auto found = probe_executable_candidate(std::filesystem::path(cand)); !found.empty()) {
            return ToolResolution{quote_cmd_if_spaces(found), found, ToolSource::Auto};
        }
    }
    return ToolResolution{"", {}, ToolSource::Auto};
}

CFIRE_INFRA_INLINE std::string resolve_emscripten_compiler_cfg() {
    return resolve_emscripten_compiler_detailed().command;
}

CFIRE_INFRA_INLINE ToolResolution resolve_emscripten_c_compiler_detailed() {
    if (const char* ev = std::getenv("CFIRE_EMCC"); ev && *ev) {
        std::string val(ev);
        return ToolResolution{val, resolve_tool_executable_path(val), ToolSource::Env};
    }
    auto emxx = resolve_emscripten_compiler_detailed();
    if (!emxx.executable_path.empty()) {
        auto sib = probe_executable_candidate(emxx.executable_path.parent_path() / "emcc");
        if (!sib.empty()) {
            return ToolResolution{quote_cmd_if_spaces(sib), sib, emxx.source};
        }
    }
    if (auto found = find_executable_in_path("emcc"); !found.empty()) {
        return ToolResolution{quote_cmd_if_spaces(found), found, ToolSource::Auto};
    }
    static constexpr const char* kEmccPaths[] = {
        "/opt/homebrew/opt/emscripten/libexec/emcc",
        "/usr/local/opt/emscripten/libexec/emcc",
        "/usr/lib/emscripten/emcc"
    };
    for (const char* cand : kEmccPaths) {
        if (auto found = probe_executable_candidate(std::filesystem::path(cand)); !found.empty()) {
            return ToolResolution{quote_cmd_if_spaces(found), found, ToolSource::Auto};
        }
    }
    return ToolResolution{"", {}, ToolSource::Auto};
}

CFIRE_INFRA_INLINE std::string resolve_emscripten_c_compiler_cfg() {
    return resolve_emscripten_c_compiler_detailed().command;
}

CFIRE_INFRA_INLINE ToolResolution resolve_archiver_detailed(std::string_view target_triple) {
    if (auto ov = resolve_explicit_tool_override(
            cli_toolchain_overrides().ar, "CFIRE_AR", "ar")) {
        return *ov;
    }
    if (target_triple == "wasm" || target_triple == "web" ||
        target_triple.find("wasm") != std::string_view::npos) {
        auto emxx = resolve_emscripten_compiler_detailed();
        if (!emxx.executable_path.empty()) {
            auto sib = probe_executable_candidate(emxx.executable_path.parent_path() / "emar");
            if (!sib.empty()) {
                return ToolResolution{quote_cmd_if_spaces(sib), sib, ToolSource::Auto};
            }
        }
        if (auto found = find_executable_in_path("emar"); !found.empty()) {
            return ToolResolution{quote_cmd_if_spaces(found), found, ToolSource::Auto};
        }
    }
    bool is_non_apple_cross = !target_triple.empty() &&
        target_triple.find("apple") == std::string_view::npos &&
        target_triple != "ios" && target_triple != "ios-sim";
    if (is_non_apple_cross) {
        std::string triple(target_triple);
        for (const std::string& cand : {triple + "-llvm-ar", triple + "-ar"}) {
            if (auto found = find_executable_in_path(cand); !found.empty()) {
                return ToolResolution{quote_cmd_if_spaces(found), found, ToolSource::Auto};
            }
        }
        if (auto home = user_home_dir(); !home.empty()) {
            std::filesystem::path opt_dir = home / ".local" / "opt";
            std::error_code ec;
            if (std::filesystem::is_directory(opt_dir, ec)) {
                for (const auto& entry : std::filesystem::directory_iterator(opt_dir, ec)) {
                    if (!entry.is_directory(ec)) continue;
                    for (const std::string& prefixed : {triple + "-llvm-ar", triple + "-ar", std::string("llvm-ar")}) {
                        if (auto found = probe_executable_candidate(entry.path() / "bin" / prefixed); !found.empty()) {
                            return ToolResolution{quote_cmd_if_spaces(found), found, ToolSource::Auto};
                        }
                    }
                }
            }
        }
        auto cxx = resolve_cxx_compiler_detailed(target_triple);
        if (!cxx.executable_path.empty()) {
            auto sib = probe_executable_candidate(cxx.executable_path.parent_path() / "llvm-ar");
            if (!sib.empty()) {
                return ToolResolution{quote_cmd_if_spaces(sib), sib, ToolSource::Auto};
            }
        }
        if (auto found = find_executable_in_path("llvm-ar"); !found.empty()) {
            return ToolResolution{quote_cmd_if_spaces(found), found, ToolSource::Auto};
        }
    }
    auto cxx = resolve_cxx_compiler_detailed(target_triple);
    if (!cxx.executable_path.empty()) {
        auto sib = probe_executable_candidate(cxx.executable_path.parent_path() / "llvm-ar");
        if (!sib.empty()) {
            return ToolResolution{quote_cmd_if_spaces(sib), sib, ToolSource::Auto};
        }
    }
    for (const char* cand : {"ar", "llvm-ar"}) {
        if (auto found = find_executable_in_path(cand); !found.empty()) {
            return ToolResolution{quote_cmd_if_spaces(found), found, ToolSource::Auto};
        }
    }
    return ToolResolution{"ar", {}, ToolSource::Auto};
}

CFIRE_INFRA_INLINE std::string resolve_archiver(std::string_view target_triple) {
    return resolve_archiver_detailed(target_triple).command;
}

CFIRE_INFRA_INLINE ToolResolution resolve_linker_pref_detailed() {
    if (auto ov = resolve_explicit_tool_override(
            cli_toolchain_overrides().ld, "CFIRE_LD", "ld")) {
        return *ov;
    }
    return ToolResolution{"", {}, ToolSource::Auto};
}

CFIRE_INFRA_INLINE std::string resolve_linker_pref() {
    return resolve_linker_pref_detailed().command;
}

CFIRE_INFRA_INLINE std::string format_linker_flag_from_pref(std::string_view ld_pref) {
    if (ld_pref.empty()) return "";
    if (ld_pref.starts_with("-")) return std::string(ld_pref);
    return std::string("-fuse-ld=") + std::string(ld_pref);
}

CFIRE_INFRA_INLINE std::string compiler_binary_fingerprint(const std::string& command) {
    if (auto p = probe_executable_candidate(std::filesystem::path(command)); !p.empty()) {
        return file_fingerprint_string(p);
    }
    std::string out;
    std::size_t i = 0;
    const std::size_t n = command.size();
    while (i < n) {
        while (i < n && (command[i] == ' ' || command[i] == '\t')) ++i;
        if (i >= n) break;
        std::string tok;
        if (command[i] == '"' || command[i] == '\'') {
            char q = command[i++];
            while (i < n && command[i] != q) tok.push_back(command[i++]);
            if (i < n && command[i] == q) ++i;
        } else {
            while (i < n && command[i] != ' ' && command[i] != '\t') tok.push_back(command[i++]);
        }
        if (tok.empty()) continue;
        if (!out.empty()) out.push_back('|');
        if (!tok.starts_with("-")) {
            if (auto rp = find_executable_in_path(tok); !rp.empty()) {
                out += file_fingerprint_string(rp);
                continue;
            }
        }
        out += tok;
    }
    return out.empty() ? command : out;
}

#endif // !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_TU_INFRA_IMPL)

} // namespace cfire::build

#undef CFIRE_INFRA_INLINE
