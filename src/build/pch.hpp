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
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
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
inline bool g_no_shard = false;

inline bool g_verbose_mode = false;

inline bool g_portable_mode = false;

inline bool g_static_mode = false;

inline bool g_universal_mode = false;

inline std::string g_macos_min_version{};

inline std::string g_ios_min_version{};

inline std::string g_sign_identity_override{};

inline std::string g_provisioning_profile_override{};

inline std::string g_entitlements_override{};

inline bool g_emit_ipa_mode = false;

inline constexpr std::string_view kDefaultMacosMinVersion = "13.3";

inline constexpr std::string_view kDefaultIosMinVersion = "17.0";

inline constexpr std::string_view kMinSupportedIosVersion = "16.3";

inline std::string g_sanitizer_set = "address,undefined";

[[nodiscard]] inline auto parse_macos_version_pair(std::string_view ver) -> std::optional<std::pair<int, int>> {
    if (ver.empty()) return std::nullopt;
    int parts[3] = {0, 0, 0};
    int part_idx = 0;
    std::size_t pos = 0;
    while (pos <= ver.size()) {
        if (part_idx >= 3) return std::nullopt;
        std::size_t dot = ver.find('.', pos);
        std::string_view seg = (dot == std::string_view::npos) ? ver.substr(pos) : ver.substr(pos, dot - pos);
        if (seg.empty() || seg.size() > 5) return std::nullopt;
        int val = 0;
        for (char c : seg) {
            if (c < '0' || c > '9') return std::nullopt;
            val = val * 10 + (c - '0');
        }
        parts[part_idx++] = val;
        if (dot == std::string_view::npos) break;
        pos = dot + 1;
    }
    return std::pair<int, int>{parts[0], parts[1]};
}

[[nodiscard]] inline auto is_macos_min_supported(std::string_view ver) -> bool {
    auto p = parse_macos_version_pair(ver);
    if (!p) return false;
    return p->first > 13 || (p->first == 13 && p->second >= 3);
}

[[nodiscard]] inline auto is_ios_min_supported(std::string_view ver) -> bool {
    auto p = parse_macos_version_pair(ver);
    if (!p) return false;
    return p->first > 16 || (p->first == 16 && p->second >= 3);
}

[[nodiscard]] inline auto is_apple_triple(std::string_view target_triple) noexcept -> bool {
    if (target_triple.find("apple") != std::string_view::npos) return true;
#if defined(__APPLE__)
    return target_triple.empty();
#else
    return false;
#endif
}

[[nodiscard]] inline auto is_wasm_triple(std::string_view target_triple) noexcept -> bool {
    return target_triple == "wasm" || target_triple == "web" ||
           target_triple.find("wasm") != std::string_view::npos;
}

[[nodiscard]] inline auto is_windows_triple(std::string_view target_triple) noexcept -> bool {
    if (target_triple.find("windows") != std::string_view::npos ||
        target_triple.find("win32") != std::string_view::npos ||
        target_triple.find("mingw") != std::string_view::npos) {
        return true;
    }
#if defined(_WIN32)
    return target_triple.empty();
#else
    return false;
#endif
}

[[nodiscard]] inline auto is_macos_target(
    std::string_view target_triple,
    bool is_ios,
    bool is_wasm
) noexcept -> bool {
    if (is_ios || is_wasm) return false;
    if (!target_triple.empty()) {
        return target_triple.find("apple-macos") != std::string_view::npos ||
               target_triple.find("apple-darwin") != std::string_view::npos;
    }
#if defined(__APPLE__)
    return true;
#else
    return false;
#endif
}

[[nodiscard]] inline auto ignore_system_libs() -> bool {
    const char* v = std::getenv("CFIRE_IGNORE_SYSTEM_LIBS");
    return v && (std::string_view(v) == "1" || std::string_view(v) == "true");
}

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

[[nodiscard]] inline BuildProfile current_build_profile() {
    return (g_build_profile == BuildProfile::DevDebug || g_debug_mode)
        ? BuildProfile::DevDebug
        : g_build_profile;
}

[[nodiscard]] inline bool is_dev_debug_profile() {
    return g_build_profile == BuildProfile::DevDebug || g_debug_mode;
}

[[nodiscard]] inline auto resolve_effective_macos_min(
    std::string_view target_triple,
    bool is_ios,
    bool is_wasm,
    std::string_view manifest_macos_min = {},
    bool force_for_app_bundle = false
) -> std::string {
    if (!is_macos_target(target_triple, is_ios, is_wasm)) return {};
    if (!g_macos_min_version.empty()) return g_macos_min_version;
    if (!manifest_macos_min.empty()) return std::string(manifest_macos_min);
    if (auto pos = target_triple.find("apple-macos"); pos != std::string_view::npos) {
        std::string_view rest = target_triple.substr(pos + std::string_view("apple-macos").size());
        if (!rest.empty() && parse_macos_version_pair(rest).has_value()) {
            return std::string(rest);
        }
    }
    if (g_portable_mode || g_universal_mode || force_for_app_bundle || !is_dev_debug_profile()) {
        return std::string(kDefaultMacosMinVersion);
    }
    return {};
}

[[nodiscard]] inline auto resolve_effective_ios_min(
    std::string_view target_triple,
    std::string_view manifest_ios_min = {}
) -> std::string {
    if (!g_ios_min_version.empty()) return g_ios_min_version;
    if (!manifest_ios_min.empty()) return std::string(manifest_ios_min);
    if (auto pos = target_triple.find("-apple-ios"); pos != std::string_view::npos) {
        std::string_view rest = target_triple.substr(pos + std::string_view("-apple-ios").size());
        if (auto dash = rest.find('-'); dash != std::string_view::npos) {
            rest = rest.substr(0, dash);
        }
        if (!rest.empty() && parse_macos_version_pair(rest).has_value()) {
            return std::string(rest);
        }
    }
    return std::string(kDefaultIosMinVersion);
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

[[nodiscard]] inline auto pch_instantiate_templates_flag() -> std::string_view {
#if defined(__linux__)
    // Clang + GNU libstdc++ <format> drops inline virtual sink definitions (e.g. _Seq_sink::_M_bump)
    // in consuming TUs at -O0/-O1 when templates are pre-instantiated in a PCH without -fpch-codegen.
    return "";
#else
    return " -fpch-instantiate-templates";
#endif
}

inline constexpr uint64_t kDefaultPchCacheMaxBytes = 2048ULL * 1024ULL * 1024ULL; // 2 GiB

[[nodiscard]] CFIRE_INFRA_INLINE auto pch_cache_dir() -> std::filesystem::path;

[[nodiscard]] CFIRE_INFRA_INLINE auto configured_pch_max_bytes() -> uint64_t;

CFIRE_INFRA_INLINE void evict_pch_cache_to_budget(
    const std::filesystem::path& pch_dir,
    uint64_t max_bytes,
    std::string_view preserve_stem = ""
);

[[nodiscard]] CFIRE_INFRA_INLINE auto filter_pch_relevant_cflags(
    const std::filesystem::path& runtime_dir,
    const std::vector<std::string>& cflags
) -> std::vector<std::string>;

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

#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_INFRA_IMPL)

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
        if (line.empty() || line.starts_with("//") || line == "#pragma once") {
            continue;
        }
        if (!saw_prelude && (line.starts_with("#ifndef ") || line == "#endif")) {
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

[[nodiscard]] CFIRE_INFRA_INLINE auto configured_pch_max_bytes() -> uint64_t {
    if (const char* env_mb = std::getenv("CFIRE_PCH_CACHE_MAX_MB"); env_mb && *env_mb) {
        long long mb = std::atoll(env_mb);
        if (mb <= 0) return 0;
        return static_cast<uint64_t>(mb) * 1024ULL * 1024ULL;
    }
    return kDefaultPchCacheMaxBytes;
}

CFIRE_INFRA_INLINE void evict_pch_cache_to_budget(
    const std::filesystem::path& pch_dir,
    uint64_t max_bytes,
    std::string_view preserve_stem
) {
    if (max_bytes == 0) return;
    std::error_code ec;
    if (pch_dir.empty() || !std::filesystem::exists(pch_dir, ec) || ec) return;

    struct PchEntry {
        std::string stem;
        std::filesystem::path pch_path;
        std::filesystem::path hpp_path;
        uint64_t size = 0;
        int64_t mtime_ns = 0;
    };
    std::vector<PchEntry> items;
    uint64_t total_size = 0;
    const auto now_ft = std::filesystem::file_time_type::clock::now();

    for (const auto& de : std::filesystem::directory_iterator(pch_dir, ec)) {
        if (ec) break;
        if (!de.is_regular_file(ec)) continue;
        const auto name = de.path().filename().string();
        if (name.contains(".tmp.")) {
            auto ft = de.last_write_time(ec);
            if (!ec && now_ft - ft > std::chrono::minutes(10)) {
                std::filesystem::remove(de.path(), ec);
            }
            ec.clear();
            continue;
        }
        if (de.path().extension() != ".pch") continue;
        uint64_t sz = de.file_size(ec);
        if (ec) continue;
        auto ft = de.last_write_time(ec);
        int64_t ns = ec ? 0 : std::chrono::duration_cast<std::chrono::nanoseconds>(
            ft.time_since_epoch()
        ).count();
        std::string stem = de.path().stem().string();
        items.push_back(PchEntry{
            stem,
            de.path(),
            pch_dir / (stem + ".hpp"),
            sz,
            ns
        });
        total_size += sz;
    }

    if (total_size <= max_bytes) return;

    std::sort(items.begin(), items.end(), [](const PchEntry& a, const PchEntry& b) {
        return a.mtime_ns < b.mtime_ns;
    });

    for (const auto& item : items) {
        if (total_size <= max_bytes) break;
        if (!preserve_stem.empty() && item.stem == preserve_stem) continue;
        auto lock_dir = std::filesystem::path(item.pch_path.string() + ".lock");
        if (std::filesystem::exists(lock_dir, ec)) continue;
        std::filesystem::remove(item.pch_path, ec);
        std::filesystem::remove(item.hpp_path, ec);
        if (total_size >= item.size) total_size -= item.size;
        else total_size = 0;
    }
}

[[nodiscard]] CFIRE_INFRA_INLINE auto filter_pch_relevant_cflags(
    const std::filesystem::path& runtime_dir,
    const std::vector<std::string>& cflags
) -> std::vector<std::string> {
    std::error_code ec;
    auto can_rt = runtime_dir.empty() ? std::filesystem::path{} : std::filesystem::weakly_canonical(runtime_dir, ec);
    if (ec || can_rt.empty()) can_rt = runtime_dir;
    const std::string rt_str = can_rt.lexically_normal().generic_string();

    ec.clear();
    auto raw_tmp = std::filesystem::temp_directory_path(ec);
    std::string tmp_str;
    if (!ec && !raw_tmp.empty()) {
        auto can_tmp = std::filesystem::weakly_canonical(raw_tmp, ec);
        if (ec || can_tmp.empty()) can_tmp = raw_tmp;
        tmp_str = can_tmp.lexically_normal().generic_string();
        while (tmp_str.size() > 1 && tmp_str.back() == '/') tmp_str.pop_back();
    }

    auto path_starts_with_dir = [](std::string_view p, std::string_view dir) -> bool {
        if (dir.empty() || !p.starts_with(dir)) return false;
        return p.size() == dir.size() || p[dir.size()] == '/';
    };

    auto is_ephemeral_include_dir = [&](std::string_view inc_raw) -> bool {
        while (inc_raw.size() >= 2 && inc_raw.front() == '"' && inc_raw.back() == '"') {
            inc_raw = inc_raw.substr(1, inc_raw.size() - 2);
        }
        if (inc_raw.empty()) return false;
        std::error_code pec;
        auto can_p = std::filesystem::weakly_canonical(std::filesystem::path(inc_raw), pec);
        std::string norm = (pec || can_p.empty())
            ? std::filesystem::path(inc_raw).lexically_normal().generic_string()
            : can_p.lexically_normal().generic_string();
        if (!rt_str.empty() && path_starts_with_dir(norm, rt_str)) return false;
        if (!tmp_str.empty() && path_starts_with_dir(norm, tmp_str)) return true;
        return false;
    };

    std::vector<std::string> out;
    out.reserve(cflags.size());
    for (std::size_t i = 0; i < cflags.size(); ++i) {
        const auto& f = cflags[i];
        if (f == "-I" && i + 1 < cflags.size()) {
            if (is_ephemeral_include_dir(cflags[i + 1])) {
                ++i;
                continue;
            }
            out.push_back(f);
            out.push_back(cflags[++i]);
            continue;
        }
        if (f.starts_with("-I") && f.size() > 2) {
            if (is_ephemeral_include_dir(std::string_view(f).substr(2))) {
                continue;
            }
        }
        out.push_back(f);
    }
    return out;
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
    auto filtered_cflags = filter_pch_relevant_cflags(runtime_dir, cflags);

    std::string key = std::format(
        "v5|rt={}|rtfp={}|cc={}|opt={}|st={}|it={}\n",
        (ec ? runtime_dir : can_rt).string(),
        rt_fp,
        compiler_fp,
        opt_flags,
        pch_static_flag(),
        pch_instantiate_templates_flag()
    );
    for (const auto& cf : filtered_cflags) {
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
    auto filtered_cflags = filter_pch_relevant_cflags(runtime_dir, cflags);
    std::string stem = compute_composite_pch_stem(runtime_dir, umbrella_content, opt_flags, filtered_cflags);
    auto umb_hpp = pch_dir / (stem + ".hpp");
    auto pch_path = pch_dir / (stem + ".pch");

    auto maybe_prune_once_for_dir = [&](std::string_view keep_stem) {
        const uint64_t budget = configured_pch_max_bytes();
        if (budget == 0) return;
        static std::mutex prune_mu;
        static std::filesystem::path last_pruned_dir;
        bool should_prune = false;
        {
            std::lock_guard<std::mutex> lk(prune_mu);
            if (last_pruned_dir != pch_dir) {
                last_pruned_dir = pch_dir;
                should_prune = true;
            }
        }
        if (should_prune) {
            evict_pch_cache_to_budget(pch_dir, budget, keep_stem);
        }
    };

    auto touch_hit_mtime = [&]() {
        std::error_code mec;
        auto ft = std::filesystem::last_write_time(pch_path, mec);
        const auto now_ft = std::filesystem::file_time_type::clock::now();
        if (!mec && now_ft - ft > std::chrono::minutes(5)) {
            std::filesystem::last_write_time(pch_path, now_ft, mec);
        }
    };

    auto pch_ready = [&]() {
        std::error_code pec;
        return std::filesystem::exists(pch_path, pec) && !pec &&
               std::filesystem::file_size(pch_path, pec) > 0 && !pec;
    };
    if (pch_ready()) {
        touch_hit_mtime();
        maybe_prune_once_for_dir(stem);
        return pch_path.string();
    }

    detail::PchDirLockGuard lock(pch_path);
    if (pch_ready()) {
        touch_hit_mtime();
        maybe_prune_once_for_dir(stem);
        return pch_path.string();
    }

    if (!atomic_write_text_file(umb_hpp, umbrella_content)) {
        return "";
    }

    std::string cxx_bin = resolve_cxx_compiler();
    auto tmp_pch_path = pch_dir / std::format("{}.tmp.{}.pch", stem, current_pid());
    std::string cmd = cxx_bin;
    for (const auto& f : filtered_cflags) {
        cmd += " " + f;
    }
    cmd += std::format(
        " {}{}{} -x c++-header \"{}\" -o \"{}\"",
        opt_flags,
        pch_static_flag(),
        pch_instantiate_templates_flag(),
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
    if (const uint64_t budget = configured_pch_max_bytes(); budget > 0) {
        evict_pch_cache_to_budget(pch_dir, budget, stem);
    }
    return pch_path.string();
}

[[nodiscard]] CFIRE_INFRA_INLINE auto build_default_pch_cflags(
    const std::filesystem::path& runtime_dir,
    bool sanitize,
    bool test_mode,
    bool with_pthread
) -> std::pair<std::string, std::vector<std::string>> {
    std::vector<std::string> cflags = {"-std=c++2b", "-Wno-c++11-narrowing", "-DSIMDJSON_DEVELOPMENT_CHECKS=0", "-I" + runtime_dir.string()};
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
    if (!test_mode && !is_dev_debug_profile()) {
        cflags.push_back("-DCFIRE_RELEASE_BUILD=1");
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

#endif // !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_INFRA_IMPL)

} // namespace cfire::build

#undef CFIRE_INFRA_INLINE
