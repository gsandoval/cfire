#pragma once
// src/build/pch.hpp — Build Profiles & Content-Hashed Composite Runtime PCH Pipeline

#include "../version.hpp"
#include "../clang_bridge.hpp"
#include "cache_paths.hpp"
#include "toolchain_config.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
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

enum class BuildProfile {
    Release,
    DevDebug
};

inline BuildProfile g_build_profile = BuildProfile::Release;

inline bool g_debug_mode = false;

inline bool g_profile_mode = false;

inline bool g_no_trap = false;

inline bool g_emit_dsym = false;

inline bool g_no_binary_cache = false;

inline bool g_verbose_mode = false;

inline std::string g_sanitizer_set = "address,undefined";

[[nodiscard]] inline bool is_verbose_build() {
    if (g_verbose_mode) return true;
    const char* v = std::getenv("CFIRE_VERBOSE");
    return v && std::string_view(v) == "1";
}

[[nodiscard]] inline auto explicit_entry_file_only() -> bool& {
    static bool value = false;
    return value;
}

inline void set_build_profile(BuildProfile profile) {
    g_build_profile = profile;
    g_debug_mode = (profile == BuildProfile::DevDebug);
}

[[nodiscard]] inline bool is_dev_debug_profile() {
    return g_build_profile == BuildProfile::DevDebug || g_debug_mode;
}

[[nodiscard]] inline auto sanitizer_opt_flags() -> std::string {
    return "-O1 -g -fno-omit-frame-pointer -fsanitize=" + g_sanitizer_set;
}

[[nodiscard]] inline auto pch_static_flag() -> std::string_view {
#if defined(_WIN32)
    return " -static";
#else
    return "";
#endif
}

[[nodiscard]] CFIRE_INFRA_INLINE auto pch_cache_dir() -> std::filesystem::path;

[[nodiscard]] CFIRE_INFRA_INLINE auto runtime_headers_fingerprint(const std::filesystem::path& runtime_dir) -> std::string;

[[nodiscard]] CFIRE_INFRA_INLINE auto extract_pch_umbrella_header(std::string_view cpp_code) -> std::string;

[[nodiscard]] CFIRE_INFRA_INLINE auto compute_composite_pch_stem(
    const std::filesystem::path& runtime_dir,
    std::string_view umbrella_content,
    const std::string& opt_flags,
    const std::vector<std::string>& cflags
) -> std::string;

[[nodiscard]] CFIRE_INFRA_INLINE auto ensure_composite_pch(
    const std::filesystem::path& runtime_dir,
    std::string_view umbrella_content,
    const std::string& opt_flags,
    const std::vector<std::string>& cflags
) -> std::string;

[[nodiscard]] CFIRE_INFRA_INLINE auto build_default_pch_cflags(
    const std::filesystem::path& runtime_dir,
    bool sanitize,
    bool test_mode,
    bool with_pthread
) -> std::pair<std::string, std::vector<std::string>>;

[[nodiscard]] CFIRE_INFRA_INLINE auto prelude_pch_path(
    const std::filesystem::path& runtime_dir,
    bool sanitize,
    bool test_mode = false,
    bool with_pthread = false
) -> std::filesystem::path;

[[nodiscard]] CFIRE_INFRA_INLINE auto ensure_prelude_pch(
    const std::filesystem::path& runtime_dir,
    bool sanitize,
    bool test_mode = false,
    bool with_pthread = false
) -> std::string;

#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_TU_INFRA_IMPL)

[[nodiscard]] CFIRE_INFRA_INLINE auto pch_cache_dir() -> std::filesystem::path {
    auto dir = cache_subdir("pch");
#if !defined(_WIN32)
    ::chmod(dir.c_str(), 0700);
#endif
    return dir;
}

[[nodiscard]] CFIRE_INFRA_INLINE auto runtime_headers_fingerprint(const std::filesystem::path& runtime_dir) -> std::string {
    std::error_code ec;
    if (runtime_dir.empty() || !std::filesystem::exists(runtime_dir, ec) || ec) {
        return "missing";
    }
    std::vector<std::string> stamps;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(runtime_dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        auto ext = entry.path().extension();
        if (ext != ".hpp" && ext != ".h") continue;
        int64_t mtime_ns = 0;
        uint64_t sz = 0;
        if (!file_stat_fingerprint(entry.path(), mtime_ns, sz)) continue;
        auto rel = std::filesystem::relative(entry.path(), runtime_dir, ec);
        std::string rel_s = ec ? entry.path().filename().string() : rel.generic_string();
        stamps.push_back(std::format("{}|{}|{}", rel_s, mtime_ns, sz));
    }
    std::sort(stamps.begin(), stamps.end());
    std::string combined;
    for (const auto& s : stamps) {
        combined += s;
        combined.push_back('\n');
    }
    return sha256_hex(combined);
}

[[nodiscard]] CFIRE_INFRA_INLINE auto extract_pch_umbrella_header(std::string_view cpp_code) -> std::string {
    std::string out = "#pragma once\n";
    bool saw_prelude = false;
    size_t pos = 0;
    while (pos < cpp_code.size()) {
        size_t nl = cpp_code.find('\n', pos);
        std::string_view raw_line = (nl == std::string_view::npos)
            ? cpp_code.substr(pos)
            : cpp_code.substr(pos, nl - pos);
        pos = (nl == std::string_view::npos) ? cpp_code.size() : nl + 1;

        while (!raw_line.empty() && (raw_line.back() == '\r' || raw_line.back() == ' ' || raw_line.back() == '\t')) {
            raw_line.remove_suffix(1);
        }
        std::string_view line = raw_line;
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
            line.remove_prefix(1);
        }
        if (line.empty() || line.starts_with("//")) {
            continue;
        }
        if (line.starts_with("#define ")) {
            std::string_view rest = line.substr(8);
            while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) {
                rest.remove_prefix(1);
            }
            size_t id_len = 0;
            while (id_len < rest.size()) {
                char c = rest[id_len];
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '_') {
                    ++id_len;
                } else {
                    break;
                }
            }
            if (id_len > 0) {
                std::string_view macro = rest.substr(0, id_len);
                out += std::format("#ifndef {}\n{}\n#endif\n", macro, line);
                continue;
            }
            break;
        }
        if (line == "#include \"cfire_prelude.hpp\"") {
            saw_prelude = true;
            out += "#include \"cfire_prelude.hpp\"\n";
            continue;
        }
        if (line.starts_with("#include \"core/") && line.ends_with("\"")) {
            out += std::string(line);
            out.push_back('\n');
            continue;
        }
        break;
    }
    return saw_prelude ? out : std::string{};
}

namespace detail {

struct PchDirLockGuard {
    std::filesystem::path lock_dir;
    bool owned = false;

    explicit PchDirLockGuard(const std::filesystem::path& pch_path)
        : lock_dir(std::filesystem::path(pch_path.string() + ".lock")) {
        std::error_code ec;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (std::chrono::steady_clock::now() < deadline) {
            if (std::filesystem::create_directory(lock_dir, ec) && !ec) {
                owned = true;
                return;
            }
            // Break stale locks left behind by a crashed builder (> 60s old).
            if (std::filesystem::exists(lock_dir, ec)) {
                auto wt = std::filesystem::last_write_time(lock_dir, ec);
                if (!ec && std::filesystem::file_time_type::clock::now() - wt > std::chrono::seconds(60)) {
                    std::filesystem::remove_all(lock_dir, ec);
                    continue;
                }
            }
            if (std::filesystem::exists(pch_path, ec) && !ec &&
                std::filesystem::file_size(pch_path, ec) > 0 && !ec) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    ~PchDirLockGuard() {
        if (owned) {
            std::error_code ec;
            std::filesystem::remove_all(lock_dir, ec);
        }
    }
};

}

[[nodiscard]] CFIRE_INFRA_INLINE auto compute_composite_pch_stem(
    const std::filesystem::path& runtime_dir,
    std::string_view umbrella_content,
    const std::string& opt_flags,
    const std::vector<std::string>& cflags
) -> std::string {
    std::error_code ec;
    auto can_rt = std::filesystem::weakly_canonical(runtime_dir, ec);
    std::string cxx_bin = resolve_cxx_compiler();
    std::string compiler_fp = compiler_binary_fingerprint(cxx_bin);
    std::string rt_fp = runtime_headers_fingerprint(runtime_dir);

    std::string key = std::format(
        "v3|rt={}|rtfp={}|cc={}|ver={}|sha={}|opt={}|st={}\n",
        (ec ? runtime_dir : can_rt).string(),
        rt_fp,
        compiler_fp,
        cfire::version::kVersion,
        cfire::version::kGitSha,
        opt_flags,
        pch_static_flag()
    );
    for (const auto& cf : cflags) {
        key += "cf:";
        key += cf;
        key.push_back('\n');
    }
    key += "umb:\n";
    key += umbrella_content;

    std::string h16 = sha256_hex(key).substr(0, 16);
    std::string prof_tag = opt_flags.contains("-fsanitize=thread") ? "tsan"
        : (opt_flags.contains("-fsanitize=address") ? "asan"
        : (opt_flags.contains("-O0") ? "debug" : "rel"));
    return std::format("cfire_{}_{}", prof_tag, h16);
}

[[nodiscard]] CFIRE_INFRA_INLINE auto ensure_composite_pch(
    const std::filesystem::path& runtime_dir,
    std::string_view umbrella_content,
    const std::string& opt_flags,
    const std::vector<std::string>& cflags
) -> std::string {
    if (umbrella_content.empty()) return "";
    std::error_code ec;
    auto prelude_hdr = runtime_dir / "cfire_prelude.hpp";
    if (!std::filesystem::exists(prelude_hdr, ec) || ec) return "";

    auto pch_dir = pch_cache_dir();
    std::string stem = compute_composite_pch_stem(runtime_dir, umbrella_content, opt_flags, cflags);
    auto umb_hpp = pch_dir / (stem + ".hpp");
    auto pch_path = pch_dir / (stem + ".pch");

    auto pch_ready = [&]() {
        std::error_code pec;
        return std::filesystem::exists(pch_path, pec) && !pec &&
               std::filesystem::file_size(pch_path, pec) > 0 && !pec;
    };
    if (pch_ready()) {
        return pch_path.string();
    }

    detail::PchDirLockGuard lock(pch_path);
    if (pch_ready()) {
        return pch_path.string();
    }

    if (!atomic_write_text_file(umb_hpp, umbrella_content)) {
        return "";
    }

    std::string cxx_bin = resolve_cxx_compiler();
    auto tmp_pch_path = pch_dir / std::format("{}.tmp.{}.pch", stem, current_pid());
    std::string cmd = cxx_bin;
    for (const auto& f : cflags) {
        cmd += " " + f;
    }
    cmd += std::format(
        " {}{} -fpch-instantiate-templates -x c++-header \"{}\" -o \"{}\"",
        opt_flags,
        pch_static_flag(),
        umb_hpp.string(),
        tmp_pch_path.string()
    );
    int rc = 0;
    (void)bridge::run_command_capture(cmd, rc);
    if (rc != 0) {
        std::filesystem::remove(tmp_pch_path, ec);
        return "";
    }
    std::filesystem::rename(tmp_pch_path, pch_path, ec);
    if (ec) {
        std::filesystem::remove(tmp_pch_path, ec);
        if (!pch_ready()) return "";
    }
    return pch_path.string();
}

[[nodiscard]] CFIRE_INFRA_INLINE auto build_default_pch_cflags(
    const std::filesystem::path& runtime_dir,
    bool sanitize,
    bool test_mode,
    bool with_pthread
) -> std::pair<std::string, std::vector<std::string>> {
    std::vector<std::string> cflags = {"-std=c++2b", "-Wno-c++11-narrowing", "-I" + runtime_dir.string()};
    std::error_code ec;
    if (std::filesystem::is_directory(runtime_dir / "vendor", ec)) {
        cflags.push_back("-I" + (runtime_dir / "vendor").string());
    }
#if defined(__linux__)
    cflags.push_back("-Wno-builtin-macro-redefined");
    cflags.push_back("-D__cpp_concepts=202002L");
    cflags.push_back("-pthread");
    (void)with_pthread;
#else
    if (with_pthread) {
        cflags.push_back("-pthread");
    }
#endif
    if (test_mode) {
        cflags.push_back("-DCFIRE_TEST_MODE=1");
        if (!g_no_trap) {
            cflags.push_back("-fsanitize=signed-integer-overflow,shift,integer-divide-by-zero");
            cflags.push_back("-fsanitize-trap=all");
        } else {
            cflags.push_back("-fwrapv");
        }
    } else if (is_dev_debug_profile() && !g_no_trap && !sanitize) {
        cflags.push_back("-fsanitize=signed-integer-overflow,shift,integer-divide-by-zero");
        cflags.push_back("-fsanitize-trap=all");
    } else {
        cflags.push_back("-fwrapv");
    }

    std::string opt_flags = sanitize
        ? sanitizer_opt_flags()
        : (is_dev_debug_profile() ? "-O0 -g" : "-O2");
    return {std::move(opt_flags), std::move(cflags)};
}

[[nodiscard]] CFIRE_INFRA_INLINE auto prelude_pch_path(
    const std::filesystem::path& runtime_dir,
    bool sanitize,
    bool test_mode,
    bool with_pthread) -> std::filesystem::path {
    auto [opt_flags, cflags] = build_default_pch_cflags(runtime_dir, sanitize, test_mode, with_pthread);
    std::string umb = "#pragma once\n#include \"cfire_prelude.hpp\"\n";
    return pch_cache_dir() / (compute_composite_pch_stem(runtime_dir, umb, opt_flags, cflags) + ".pch");
}

[[nodiscard]] CFIRE_INFRA_INLINE auto ensure_prelude_pch(
    const std::filesystem::path& runtime_dir,
    bool sanitize,
    bool test_mode,
    bool with_pthread) -> std::string {
    auto [opt_flags, cflags] = build_default_pch_cflags(runtime_dir, sanitize, test_mode, with_pthread);
    return ensure_composite_pch(runtime_dir, "#pragma once\n#include \"cfire_prelude.hpp\"\n", opt_flags, cflags);
}

#endif // !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_TU_INFRA_IMPL)

} // namespace cfire::build

#undef CFIRE_INFRA_INLINE
