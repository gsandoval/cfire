#pragma once
// src/version.hpp — Single-Source-Of-Truth Version & Commit Provenance for cfire (.cf)

#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace cfire::version {

inline constexpr std::string_view kVersion = "0.11.0";

// Expanded automatically by `git archive` (e.g. GitHub release/branch tarballs) via .gitattributes export-subst.
inline constexpr std::string_view kArchiveGitSha = "$Format:%h$";

[[nodiscard]] constexpr auto is_unexpanded_format(std::string_view s) -> bool {
    return s.empty() || s.starts_with("$Format:") || s == "dev";
}

[[nodiscard]] constexpr auto resolve_git_sha() -> std::string_view {
#ifdef CFIRE_GIT_SHA
    std::string_view compiled_sha = CFIRE_GIT_SHA;
    if (!compiled_sha.empty() && compiled_sha != "dev") {
        return compiled_sha;
    }
#endif
    if (!is_unexpanded_format(kArchiveGitSha)) {
        return kArchiveGitSha;
    }
    return "dev";
}

inline constexpr std::string_view kGitSha = resolve_git_sha();

[[nodiscard]] inline auto trim_sv(std::string_view sv) -> std::string_view {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
        sv.remove_suffix(1);
    }
    return sv;
}

[[nodiscard]] inline auto strip_dirty(std::string_view sha) -> std::string_view {
    sha = trim_sv(sha);
    if (sha.ends_with("-dirty")) {
        sha.remove_suffix(6);
    }
    return sha;
}

[[nodiscard]] inline auto shas_compatible(std::string_view a, std::string_view b) -> bool {
    auto sa = strip_dirty(a);
    auto sb = strip_dirty(b);
    if (sa.empty() || sb.empty() || sa == "dev" || sb == "dev" ||
        sa.starts_with("$Format:") || sb.starts_with("$Format:")) {
        return true;
    }
    return sa == sb || sa.starts_with(sb) || sb.starts_with(sa);
}

struct RuntimeProvenance {
    std::string version{};
    std::string commit{};
    bool matched{true};
    std::string mismatch_reason{};
};

[[nodiscard]] inline auto read_runtime_provenance(const std::filesystem::path& runtime_dir) -> RuntimeProvenance {
    RuntimeProvenance prov;
    std::error_code ec;

    // 1. Check the runtime stamp written by `cfire install`, `make install`, or `install.sh`. It is not named
    // `VERSION`: the runtime dir is an include path, and on case-insensitive filesystems `#include <version>`
    // (pulled in by <bit>, <format>, ...) would find that file instead of the standard header.
    auto ver_file = runtime_dir / "cfire-runtime.version";
    if (!std::filesystem::exists(ver_file, ec)) ver_file = runtime_dir / "VERSION";  // installs before 0.16
    if (std::filesystem::exists(ver_file, ec)) {
        std::ifstream ifs(ver_file);
        std::string line;
        if (std::getline(ifs, line)) {
            std::istringstream iss(line);
            iss >> prov.version >> prov.commit;
        }
    }

    // 2. Inspect runtime/cfire_prelude.hpp header markers
    auto prelude_file = runtime_dir / "cfire_prelude.hpp";
    if (std::filesystem::exists(prelude_file, ec)) {
        std::ifstream ifs(prelude_file);
        std::string line;
        for (int i = 0; i < 25 && std::getline(ifs, line); ++i) {
            std::string_view sv = trim_sv(line);
            constexpr std::string_view kVerPrefix = "// CFIRE_RUNTIME_VERSION:";
            constexpr std::string_view kShaPrefix = "// CFIRE_RUNTIME_ARCHIVE_SHA:";
            if (sv.starts_with(kVerPrefix) && prov.version.empty()) {
                prov.version = std::string(trim_sv(sv.substr(kVerPrefix.size())));
            } else if (sv.starts_with(kShaPrefix) && prov.commit.empty()) {
                auto cand = trim_sv(sv.substr(kShaPrefix.size()));
                if (!is_unexpanded_format(cand)) {
                    prov.commit = std::string(cand);
                }
            }
        }
    }

    if (prov.version.empty()) {
        prov.version = std::string(kVersion);
    }
    if (prov.commit.empty()) {
        prov.commit = std::string(kGitSha);
    }

    if (prov.version != kVersion) {
        prov.matched = false;
        prov.mismatch_reason = std::format("runtime headers version v{} != cfire binary v{}", prov.version, kVersion);
    } else if (!shas_compatible(prov.commit, kGitSha)) {
        prov.matched = false;
        prov.mismatch_reason = std::format("runtime headers commit ({}) != cfire binary commit ({})", prov.commit, kGitSha);
    }

    return prov;
}

} // namespace cfire::version
