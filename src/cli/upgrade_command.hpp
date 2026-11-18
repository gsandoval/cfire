#pragma once
// src/cli/upgrade_command.hpp — Native `cfire upgrade` (`self-update`) Subcommand

#include "../version.hpp"
#include "../clang_bridge.hpp"
#include "../build/cache_paths.hpp"
#include "../build/toolchain_layout.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#if __has_include(<print>)
#include <print>
#endif
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifndef PROCESSOR_ARCHITECTURE_ARM64
#define PROCESSOR_ARCHITECTURE_ARM64 12
#endif
#else
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace cfire::cli::upgrade {

struct SemVer {
    int major = 0;
    int minor = 0;
    int patch = 0;

    [[nodiscard]] auto str() const -> std::string {
        return std::format("{}.{}.{}", major, minor, patch);
    }

    [[nodiscard]] auto tag() const -> std::string {
        return std::format("v{}.{}.{}", major, minor, patch);
    }
};

[[nodiscard]] inline auto trim_sv(std::string_view sv) -> std::string_view {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
        sv.remove_suffix(1);
    }
    return sv;
}

[[nodiscard]] inline auto parse_semver(std::string_view raw) -> std::optional<SemVer> {
    raw = trim_sv(raw);
    if (raw.starts_with("v") || raw.starts_with("V")) {
        raw.remove_prefix(1);
    }
    if (raw.empty()) return std::nullopt;

    auto d1 = raw.find('.');
    if (d1 == std::string_view::npos) return std::nullopt;
    auto d2 = raw.find('.', d1 + 1);
    if (d2 == std::string_view::npos) return std::nullopt;
    if (raw.find('.', d2 + 1) != std::string_view::npos) return std::nullopt;

    auto parse_part = [](std::string_view seg) -> std::optional<int> {
        if (seg.empty() || seg.size() > 9) return std::nullopt;
        int val = 0;
        for (char c : seg) {
            if (c < '0' || c > '9') return std::nullopt;
            val = val * 10 + (c - '0');
        }
        return val;
    };

    auto maj = parse_part(raw.substr(0, d1));
    auto min = parse_part(raw.substr(d1 + 1, d2 - d1 - 1));
    auto pat = parse_part(raw.substr(d2 + 1));
    if (!maj || !min || !pat) return std::nullopt;
    return SemVer{.major = *maj, .minor = *min, .patch = *pat};
}

[[nodiscard]] inline auto compare_semver(const SemVer& a, const SemVer& b) noexcept -> int {
    if (a.major != b.major) return (a.major < b.major) ? -1 : 1;
    if (a.minor != b.minor) return (a.minor < b.minor) ? -1 : 1;
    if (a.patch != b.patch) return (a.patch < b.patch) ? -1 : 1;
    return 0;
}

[[nodiscard]] inline auto validate_upgrade_arg(std::string_view what, std::string_view v) -> std::expected<void, std::string> {
    if (v.empty()) {
        return std::unexpected(std::format("error: {} must not be empty", what));
    }
    if (v.starts_with("-")) {
        return std::unexpected(std::format("error: {} '{}' must not start with '-'", what, v));
    }
    if (v.starts_with("ext::") || v.starts_with("fd::")) {
        return std::unexpected(std::format("error: {} '{}' uses a forbidden transport", what, v));
    }
    for (unsigned char c : v) {
        if (c < 0x20 || c == 0x7f) {
            return std::unexpected(std::format("error: {} contains invalid control characters", what));
        }
    }
    return {};
}

[[nodiscard]] inline auto url_decode_path(std::string_view raw) -> std::string {
    auto hex_val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    };
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '%' && i + 2 < raw.size()) {
            int hi = hex_val(raw[i + 1]);
            int lo = hex_val(raw[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(raw[i]);
    }
    return out;
}

[[nodiscard]] inline auto file_url_to_path(std::string_view url) -> std::optional<std::filesystem::path> {
    if (!url.starts_with("file://")) return std::nullopt;
    std::string_view rest = url.substr(7);
    if (rest.starts_with("localhost/")) {
        rest.remove_prefix(9);
    }
    std::string decoded = url_decode_path(rest);
#if defined(_WIN32)
    if (decoded.size() >= 3 && decoded[0] == '/' &&
        std::isalpha(static_cast<unsigned char>(decoded[1])) && decoded[2] == ':') {
        decoded.erase(decoded.begin());
    }
#endif
    return std::filesystem::path(decoded);
}

struct TargetPlatform {
    std::string os;
    std::string arch;
    std::string archive_ext;
    std::string exe_name;
};

[[nodiscard]] inline auto detect_target_platform() -> std::expected<TargetPlatform, std::string> {
    TargetPlatform p;
#if defined(_WIN32)
    p.os = "windows";
    p.archive_ext = ".zip";
    p.exe_name = "cfire.exe";
    SYSTEM_INFO si{};
    ::GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: p.arch = "x86_64"; break;
        case PROCESSOR_ARCHITECTURE_ARM64: p.arch = "arm64"; break;
        default:
            return std::unexpected(std::format("unsupported Windows CPU architecture ({})",
                static_cast<unsigned>(si.wProcessorArchitecture)));
    }
#else
    struct utsname u{};
    if (::uname(&u) != 0) {
        return std::unexpected("failed to detect host OS and CPU architecture via uname");
    }
    std::string_view sys = u.sysname;
    std::string_view mach = u.machine;
    if (sys == "Darwin") {
        p.os = "macos";
    } else if (sys == "Linux") {
        p.os = "linux";
    } else {
        return std::unexpected(std::format("cfire upgrade does not support host OS '{}'", sys));
    }
    if (mach == "arm64" || mach == "aarch64") {
        p.arch = "arm64";
    } else if (mach == "x86_64" || mach == "amd64") {
        p.arch = "x86_64";
    } else {
        return std::unexpected(std::format("cfire upgrade does not support CPU architecture '{}'", mach));
    }
    p.archive_ext = ".tar.gz";
    p.exe_name = "cfire";
#endif
    return p;
}

[[nodiscard]] inline auto is_source_workspace_layout(const cfire::build::ToolchainLayout& layout) -> bool {
    if (layout.kind == cfire::build::InstallKind::WorkspaceFallback ||
        layout.kind == cfire::build::InstallKind::EnvOverride) {
        return true;
    }
    std::error_code ec;
    auto has_repo_markers = [&](const std::filesystem::path& dir) -> bool {
        if (dir.empty()) return false;
        return std::filesystem::exists(dir / "src" / "main.cpp", ec) &&
               std::filesystem::exists(dir / "Makefile", ec);
    };
    if (has_repo_markers(layout.install_root)) return true;
    if (!layout.executable_path.empty() && has_repo_markers(layout.executable_path.parent_path())) {
        return true;
    }
    return false;
}

inline void cleanup_stale_old_binary(const std::filesystem::path& exe_path) {
#if defined(_WIN32)
    if (exe_path.empty()) return;
    std::error_code ec;
    std::filesystem::path old_bin(exe_path.wstring() + L".old");
    if (std::filesystem::exists(old_bin, ec)) {
        std::filesystem::remove(old_bin, ec);
    }
#else
    (void)exe_path;
#endif
}

[[nodiscard]] inline auto ps_single_quote(std::string_view s) -> std::string {
    std::string out = "'";
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '\'') {
            out += "''";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('\'');
    return out;
}

[[nodiscard]] inline auto extract_tag_from_text(std::string_view text) -> std::string {
    auto last_tag_pos = text.rfind("/tag/");
    std::string_view tail;
    if (last_tag_pos != std::string_view::npos) {
        tail = text.substr(last_tag_pos + 5);
    } else {
        auto last_slash = text.rfind('/');
        if (last_slash == std::string_view::npos) return {};
        tail = text.substr(last_slash + 1);
    }
    auto end = tail.find_first_of(" \t\r\n?#\"'");
    if (end != std::string_view::npos) tail = tail.substr(0, end);
    return std::string(tail);
}

[[nodiscard]] inline auto resolve_latest_tag(
    const TargetPlatform& platform,
    std::string_view base_url_override
) -> std::expected<std::string, std::string> {
    if (!base_url_override.empty()) {
        if (auto dir_opt = file_url_to_path(base_url_override)) {
            std::error_code ec;
            auto latest_file = *dir_opt / "LATEST";
            if (std::filesystem::is_regular_file(latest_file, ec)) {
                std::string txt;
                if (cfire::build::read_text_file(latest_file, txt)) {
                    if (auto sv = parse_semver(txt)) return sv->tag();
                }
            }
            auto sums_file = *dir_opt / "SHA256SUMS.txt";
            if (std::filesystem::is_regular_file(sums_file, ec)) {
                std::string sums_txt;
                if (cfire::build::read_text_file(sums_file, sums_txt)) {
                    std::optional<SemVer> best;
                    std::string suffix = std::format("-{}-{}{}", platform.os, platform.arch, platform.archive_ext);
                    std::istringstream iss(sums_txt);
                    std::string line;
                    while (std::getline(iss, line)) {
                        std::string_view sv = trim_sv(line);
                        auto sp = sv.find_first_of(" \t");
                        if (sp == std::string_view::npos) continue;
                        std::string_view fname = trim_sv(sv.substr(sp));
                        if (fname.starts_with("*")) fname.remove_prefix(1);
                        if (fname.starts_with("./")) fname.remove_prefix(2);
                        if (fname.starts_with("cfire-v") && fname.ends_with(suffix)) {
                            std::string_view ver_part = fname.substr(7, fname.size() - 7 - suffix.size());
                            if (auto parsed = parse_semver(ver_part)) {
                                if (!best || compare_semver(*parsed, *best) > 0) {
                                    best = parsed;
                                }
                            }
                        }
                    }
                    if (best) return best->tag();
                }
            }
            return std::unexpected("could not determine latest release tag from local CFIRE_RELEASE_BASE_URL; pass --version vX.Y.Z");
        }
        const char* latest_env = std::getenv("CFIRE_LATEST_RELEASE_URL");
        if (!latest_env || !*latest_env) {
            return std::unexpected("CFIRE_VERSION or --version must be passed when CFIRE_RELEASE_BASE_URL is set without CFIRE_LATEST_RELEASE_URL");
        }
    }

    std::string lookup_url = "https://github.com/gsandoval/cfire/releases/latest";
    if (const char* latest_env = std::getenv("CFIRE_LATEST_RELEASE_URL"); latest_env && *latest_env) {
        lookup_url = latest_env;
    }
    if (auto v = validate_upgrade_arg("latest release URL", lookup_url); !v) {
        return std::unexpected(v.error());
    }

#if defined(_WIN32)
    {
        int rc = 0;
        std::string cmd = std::format("curl.exe -fsSL -o NUL -w \"%{{url_effective}}\" -- {}",
            cfire::bridge::shell_quote(lookup_url));
        std::string out = cfire::bridge::run_command_capture(cmd, rc);
        std::string tag = extract_tag_from_text(trim_sv(out));
        if (rc == 0 && parse_semver(tag)) return parse_semver(tag)->tag();
    }
    {
        std::string ps_cmd = std::format(
            "powershell -NoProfile -NonInteractive -Command \"$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'; "
            "try {{ [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12 }} catch {{}}; "
            "$r=[System.Net.HttpWebRequest]::Create({}); $r.Method='HEAD'; $r.AllowAutoRedirect=$false; $r.UserAgent='cfire-upgrade'; "
            "try {{ $res=$r.GetResponse(); $loc=$res.Headers['Location']; $res.Close() }} "
            "catch [System.Net.WebException] {{ if ($_.Exception.Response) {{ $loc=$_.Exception.Response.Headers['Location']; $_.Exception.Response.Close() }} }}; "
            "if ($loc) {{ Write-Output $loc }}\"",
            ps_single_quote(lookup_url));
        int rc = 0;
        std::string out = cfire::bridge::run_command_capture(ps_cmd, rc);
        std::string tag = extract_tag_from_text(out);
        if (rc == 0 && parse_semver(tag)) return parse_semver(tag)->tag();
    }
#else
    {
        std::string proto = lookup_url.starts_with("http://") ? "" : "--proto =https --proto-redir =https ";
        std::string cmd = std::format("curl -fsSL {}-o /dev/null -w '%{{url_effective}}' -- {}",
            proto, cfire::bridge::shell_quote(lookup_url));
        int rc = 0;
        std::string out = cfire::bridge::run_command_capture(cmd, rc);
        std::string tag = extract_tag_from_text(trim_sv(out));
        if (rc == 0 && parse_semver(tag)) return parse_semver(tag)->tag();
    }
    {
        int rc = 0;
        std::string cmd = std::format("wget -S --spider --max-redirect=5 -- {} 2>&1 || wget -S --spider -- {} 2>&1",
            cfire::bridge::shell_quote(lookup_url), cfire::bridge::shell_quote(lookup_url));
        std::string out = cfire::bridge::run_command_capture(cmd, rc);
        std::istringstream iss(out);
        std::string line;
        std::string found_tag;
        while (std::getline(iss, line)) {
            std::string_view sv = trim_sv(line);
            if (sv.size() >= 9 &&
                (sv.starts_with("Location:") || sv.starts_with("location:"))) {
                std::string t = extract_tag_from_text(sv);
                if (parse_semver(t)) found_tag = parse_semver(t)->tag();
            }
        }
        if (!found_tag.empty()) return found_tag;
    }
#endif
    return std::unexpected("could not resolve latest release tag from GitHub; check your network or pass --version vX.Y.Z");
}

[[nodiscard]] inline auto download_release_file(
    std::string_view url,
    const std::filesystem::path& dest_path
) -> std::expected<void, std::string> {
    if (auto v = validate_upgrade_arg("download URL", url); !v) return std::unexpected(v.error());

    if (auto local_opt = file_url_to_path(url)) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(*local_opt, ec)) {
            return std::unexpected(std::format("release file not found: {}", local_opt->string()));
        }
        std::filesystem::copy_file(*local_opt, dest_path, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            return std::unexpected(std::format("failed to copy release file '{}': {}", local_opt->string(), ec.message()));
        }
        return {};
    }

#if defined(_WIN32)
    {
        int rc = 0;
        std::string cmd = std::format("curl.exe -fsSL -o {} -- {}",
            cfire::bridge::shell_quote(dest_path.string()),
            cfire::bridge::shell_quote(url));
        std::string out = cfire::bridge::run_command_capture(cmd, rc);
        std::error_code ec;
        if (rc == 0 && std::filesystem::is_regular_file(dest_path, ec)) return {};
        std::string ps_cmd = std::format(
            "powershell -NoProfile -NonInteractive -Command \"$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'; "
            "try {{ [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12 }} catch {{}}; "
            "Invoke-WebRequest -Uri {} -OutFile {} -UseBasicParsing\"",
            ps_single_quote(url),
            ps_single_quote(dest_path.string()));
        int ps_rc = 0;
        std::string ps_out = cfire::bridge::run_command_capture(ps_cmd, ps_rc);
        if (ps_rc == 0 && std::filesystem::is_regular_file(dest_path, ec)) return {};
        return std::unexpected(std::format("failed to download '{}':\n{}\n{}", url, trim_sv(out), trim_sv(ps_out)));
    }
#else
    std::string q_dest = cfire::bridge::shell_quote(dest_path.string());
    std::string q_url = cfire::bridge::shell_quote(url);
    std::string proto = url.starts_with("http://") ? "" : "--proto =https --proto-redir =https --tlsv1.2 ";
    int rc = 0;
    std::string out = cfire::bridge::run_command_capture(
        std::format("curl -fsSL {}-o {} -- {}", proto, q_dest, q_url), rc);
    std::error_code ec;
    if (rc == 0 && std::filesystem::is_regular_file(dest_path, ec)) return {};

    int wrc = 0;
    std::string wout = cfire::bridge::run_command_capture(
        std::format("wget -q -O {} -- {}", q_dest, q_url), wrc);
    if (wrc == 0 && std::filesystem::is_regular_file(dest_path, ec)) return {};

    return std::unexpected(std::format("failed to download '{}':\n{}\n{}", url, trim_sv(out), trim_sv(wout)));
#endif
}

[[nodiscard]] inline auto find_checksum_entry(
    const std::filesystem::path& sums_path,
    std::string_view archive_name
) -> std::expected<std::string, std::string> {
    std::string content;
    if (!cfire::build::read_text_file(sums_path, content)) {
        return std::unexpected(std::format("failed to read checksum manifest '{}'", sums_path.string()));
    }
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        std::string_view sv = trim_sv(line);
        if (sv.empty()) continue;
        auto sp = sv.find_first_of(" \t");
        if (sp == std::string_view::npos) continue;
        std::string_view hex = sv.substr(0, sp);
        if (hex.size() != 64) continue;
        bool valid_hex = std::all_of(hex.begin(), hex.end(), [](unsigned char c) {
            return std::isxdigit(c) != 0;
        });
        if (!valid_hex) continue;
        std::string_view fname = trim_sv(sv.substr(sp));
        if (fname.starts_with("*")) fname.remove_prefix(1);
        if (fname.starts_with("./")) fname.remove_prefix(2);
        if (fname == archive_name) {
            std::string lower_hex(hex);
            for (char& ch : lower_hex) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            return lower_hex;
        }
    }
    return std::unexpected(std::format("checksum entry for '{}' was not found in SHA256SUMS.txt", archive_name));
}

[[nodiscard]] inline auto unpack_release_archive(
    const std::filesystem::path& archive_path,
    const std::filesystem::path& unpack_dir,
    const TargetPlatform& platform
) -> std::expected<std::filesystem::path, std::string> {
    std::error_code ec;
    std::filesystem::create_directories(unpack_dir, ec);
    if (ec) {
        return std::unexpected(std::format("failed to create unpack directory '{}': {}", unpack_dir.string(), ec.message()));
    }

    int rc = 0;
    std::string out;
#if defined(_WIN32)
    if (platform.archive_ext == ".zip") {
        std::string ps_cmd = std::format(
            "powershell -NoProfile -NonInteractive -Command \"$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'; Expand-Archive -Path {} -DestinationPath {} -Force\"",
            ps_single_quote(archive_path.string()),
            ps_single_quote(unpack_dir.string()));
        out = cfire::bridge::run_command_capture(ps_cmd, rc);
    } else {
        out = cfire::bridge::run_command_capture(
            std::format("tar -xzf {} -C {}",
                cfire::bridge::shell_quote(archive_path.string()),
                cfire::bridge::shell_quote(unpack_dir.string())),
            rc);
    }
#else
    (void)platform;
    out = cfire::bridge::run_command_capture(
        std::format("tar -xzf {} -C {}",
            cfire::bridge::shell_quote(archive_path.string()),
            cfire::bridge::shell_quote(unpack_dir.string())),
        rc);
#endif
    if (rc != 0) {
        return std::unexpected(std::format("failed to extract release archive '{}':\n{}", archive_path.string(), trim_sv(out)));
    }

    if (std::filesystem::is_regular_file(unpack_dir / platform.exe_name, ec) &&
        std::filesystem::is_directory(unpack_dir / "runtime", ec)) {
        return unpack_dir;
    }

    std::filesystem::path found_root;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(unpack_dir, ec)) {
        if (ec) break;
        if (entry.is_regular_file(ec) && entry.path().filename() == platform.exe_name) {
            auto cand = entry.path().parent_path();
            if (std::filesystem::is_regular_file(cand / "runtime" / "cfire_prelude.hpp", ec)) {
                found_root = cand;
                break;
            }
        }
    }
    if (found_root.empty()) {
        return std::unexpected(std::format("release archive did not contain '{}' with runtime/cfire_prelude.hpp", platform.exe_name));
    }
    return found_root;
}

[[nodiscard]] inline auto swap_runtime_dir_atomic(
    const std::filesystem::path& src_rt,
    const std::filesystem::path& dest_rt,
    std::string_view version_str,
    std::string_view tag_str
) -> std::expected<void, std::string> {
    std::error_code ec;
    auto parent = dest_rt.parent_path();
    if (parent.empty()) {
        return std::unexpected(std::format("invalid runtime destination path '{}'", dest_rt.string()));
    }
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        return std::unexpected(std::format("failed to prepare runtime parent '{}': {}", parent.string(), ec.message()));
    }

    auto pid = cfire::build::current_pid();
    auto stage_rt = parent / std::format(".runtime.upgrade.tmp.{}", pid);
    auto old_rt = parent / std::format(".runtime.upgrade.old.{}", pid);
    std::filesystem::remove_all(stage_rt, ec);
    std::filesystem::remove_all(old_rt, ec);

    std::filesystem::copy(
        src_rt,
        stage_rt,
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
        ec);
    if (ec) {
        std::filesystem::remove_all(stage_rt, ec);
        return std::unexpected(std::format("failed to stage runtime headers into '{}': {}", stage_rt.string(), ec.message()));
    }
    std::filesystem::remove(stage_rt / "VERSION", ec);
    if (!std::filesystem::is_regular_file(stage_rt / "cfire-runtime.version", ec)) {
        std::ofstream ver_ofs(stage_rt / "cfire-runtime.version", std::ios::trunc);
        if (ver_ofs) {
            ver_ofs << std::format("{} {}\n", version_str, tag_str);
        }
    }

    bool moved_old = false;
    if (std::filesystem::exists(dest_rt, ec)) {
        std::filesystem::rename(dest_rt, old_rt, ec);
        if (ec) {
            std::string err_msg = ec.message();
            std::filesystem::remove_all(stage_rt, ec);
            return std::unexpected(std::format("failed to move existing runtime '{}': {}", dest_rt.string(), err_msg));
        }
        moved_old = true;
    }

    std::filesystem::rename(stage_rt, dest_rt, ec);
    if (ec) {
        std::string err_msg = ec.message();
        std::error_code rollback_ec;
        if (moved_old && !std::filesystem::exists(dest_rt, rollback_ec)) {
            std::filesystem::rename(old_rt, dest_rt, rollback_ec);
        }
        std::filesystem::remove_all(stage_rt, rollback_ec);
        return std::unexpected(std::format("failed to activate new runtime '{}': {}", dest_rt.string(), err_msg));
    }

    if (moved_old) {
        std::filesystem::remove_all(old_rt, ec);
    }
    return {};
}

[[nodiscard]] inline auto swap_binary_atomic(
    const std::filesystem::path& src_bin,
    const std::filesystem::path& dest_bin
) -> std::expected<void, std::string> {
    std::error_code ec;
    auto parent = dest_bin.parent_path();
    if (parent.empty()) {
        return std::unexpected(std::format("invalid binary destination path '{}'", dest_bin.string()));
    }
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        return std::unexpected(std::format("failed to prepare binary directory '{}': {}", parent.string(), ec.message()));
    }

    auto pid = cfire::build::current_pid();
#if defined(_WIN32)
    auto tmp_bin = parent / std::format(".cfire.upgrade.{}.tmp.exe", pid);
    std::filesystem::path old_bin(dest_bin.wstring() + L".old");
    std::filesystem::remove(tmp_bin, ec);
    std::filesystem::copy_file(src_bin, tmp_bin, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        std::filesystem::remove(tmp_bin, ec);
        return std::unexpected(std::format("failed to stage updated binary '{}': {}", tmp_bin.string(), ec.message()));
    }
    if (std::filesystem::exists(old_bin, ec)) {
        std::filesystem::remove(old_bin, ec);
    }
    bool moved_old = false;
    if (std::filesystem::exists(dest_bin, ec)) {
        if (!::MoveFileExW(dest_bin.c_str(), old_bin.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DWORD err = ::GetLastError();
            std::filesystem::remove(tmp_bin, ec);
            return std::unexpected(std::format("failed to rename running binary '{}' (Win32 error {})", dest_bin.string(), err));
        }
        moved_old = true;
    }
    if (!::MoveFileExW(tmp_bin.c_str(), dest_bin.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DWORD err = ::GetLastError();
        if (moved_old) {
            ::MoveFileExW(old_bin.c_str(), dest_bin.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
        std::filesystem::remove(tmp_bin, ec);
        return std::unexpected(std::format("failed to install new binary '{}' (Win32 error {})", dest_bin.string(), err));
    }
    std::filesystem::remove(old_bin, ec);
#else
    auto tmp_bin = parent / std::format(".cfire.upgrade.{}.tmp", pid);
    std::filesystem::remove(tmp_bin, ec);
    std::filesystem::copy_file(src_bin, tmp_bin, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        std::filesystem::remove(tmp_bin, ec);
        return std::unexpected(std::format("failed to stage updated binary '{}': {}", tmp_bin.string(), ec.message()));
    }
    std::filesystem::permissions(
        tmp_bin,
        std::filesystem::perms::owner_all |
        std::filesystem::perms::group_read | std::filesystem::perms::group_exec |
        std::filesystem::perms::others_read | std::filesystem::perms::others_exec,
        ec);
    std::filesystem::rename(tmp_bin, dest_bin, ec);
    if (ec) {
        std::string err_msg = ec.message();
        std::filesystem::remove(tmp_bin, ec);
        return std::unexpected(std::format("failed to replace binary '{}': {}", dest_bin.string(), err_msg));
    }
#endif
    return {};
}

[[nodiscard]] inline auto is_interactive_tty() noexcept -> bool {
    return cfire::detail::fd_is_tty(0);
}

[[nodiscard]] inline auto run_upgrade_command(
    int argc,
    char** argv,
    const cfire::build::ToolchainLayout& layout
) -> int {
    bool check_only = false;
    bool assume_yes = false;
    bool force = false;
    std::string requested_version;

    for (int i = 2; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--check" || arg == "-c") {
            check_only = true;
        } else if (arg == "--yes" || arg == "-y") {
            assume_yes = true;
        } else if (arg == "--force" || arg == "-f") {
            force = true;
        } else if (arg == "--version" && i + 1 < argc) {
            requested_version = argv[++i];
        } else if (arg.starts_with("--version=")) {
            requested_version = std::string(arg.substr(10));
        } else {
            std::println(stderr, "error: unknown option '{}' for 'cfire upgrade'", arg);
            std::println(stderr, "Usage: cfire upgrade [--check] [--version <tag>] [--yes] [--force]");
            return 1;
        }
    }

    if (requested_version.empty()) {
        if (const char* env_ver = std::getenv("CFIRE_VERSION"); env_ver && *env_ver) {
            requested_version = env_ver;
        }
    }

    std::string_view current_ver_str = cfire::version::kVersion;
    if (const char* cur_env = std::getenv("CFIRE_UPGRADE_CURRENT_VERSION"); cur_env && *cur_env) {
        current_ver_str = cur_env;
    }
    auto cur_semver = parse_semver(current_ver_str);
    if (!cur_semver) {
        std::println(stderr, "error: invalid current version '{}'", current_ver_str);
        return 1;
    }

    if (!check_only && !force && is_source_workspace_layout(layout)) {
        std::println(stderr,
            "error: cfire is running from a source workspace checkout or CFIRE_RUNTIME_DIR override ({});\n"
            "  run 'git pull && make build' to update a local checkout, or pass '--force' to overwrite it.",
            layout.install_root.string());
        return 1;
    }

    auto platform_res = detect_target_platform();
    if (!platform_res) {
        std::println(stderr, "error: {}", platform_res.error());
        return 1;
    }
    const auto& platform = *platform_res;

    std::string base_url;
    if (const char* base_env = std::getenv("CFIRE_RELEASE_BASE_URL"); base_env && *base_env) {
        base_url = base_env;
        while (!base_url.empty() && base_url.back() == '/') base_url.pop_back();
        if (auto v = validate_upgrade_arg("CFIRE_RELEASE_BASE_URL", base_url); !v) {
            std::println(stderr, "{}", v.error());
            return 1;
        }
    }

    std::string target_tag;
    if (!requested_version.empty()) {
        if (auto v = validate_upgrade_arg("version", requested_version); !v) {
            std::println(stderr, "{}", v.error());
            return 1;
        }
        auto parsed_req = parse_semver(requested_version);
        if (!parsed_req) {
            std::println(stderr, "error: invalid release version '{}' (expected vX.Y.Z or X.Y.Z)", requested_version);
            return 1;
        }
        target_tag = parsed_req->tag();
    } else {
        auto tag_res = resolve_latest_tag(platform, base_url);
        if (!tag_res) {
            std::println(stderr, "error: {}", tag_res.error());
            return 1;
        }
        target_tag = *tag_res;
    }

    auto target_semver = parse_semver(target_tag);
    if (!target_semver) {
        std::println(stderr, "error: resolved invalid release tag '{}'", target_tag);
        return 1;
    }

    const int cmp = compare_semver(*target_semver, *cur_semver);
    if (check_only) {
        if (cmp > 0) {
            std::println("Update available: {} -> {}", cur_semver->tag(), target_semver->tag());
        } else if (cmp == 0) {
            std::println("Up to date: cfire {} is the latest release", cur_semver->tag());
        } else {
            std::println("Up to date: current {} is newer than target {}", cur_semver->tag(), target_semver->tag());
        }
        return 0;
    }

    if (!force) {
        if (requested_version.empty() && cmp <= 0) {
            std::println("✓ cfire {} is already up to date (latest: {}).", cur_semver->tag(), target_semver->tag());
            return 0;
        }
        if (!requested_version.empty() && cmp == 0) {
            std::println("✓ cfire {} is already installed (pass --force to reinstall).", cur_semver->tag());
            return 0;
        }
    }

    if (layout.executable_path.empty() || layout.runtime_dir.empty()) {
        std::println(stderr, "error: could not locate installed cfire binary or runtime directory");
        return 1;
    }

    std::error_code ec;
    auto bin_parent = layout.executable_path.parent_path();
    auto rt_parent = layout.runtime_dir.parent_path();
    if (!cfire::build::is_dir_writable(bin_parent)) {
        std::println(stderr, "error: binary directory '{}' is not writable (try elevated permissions or user prefix)", bin_parent.string());
        return 1;
    }
    if (!cfire::build::is_dir_writable(rt_parent)) {
        std::println(stderr, "error: runtime parent directory '{}' is not writable", rt_parent.string());
        return 1;
    }

    if (!assume_yes && is_interactive_tty()) {
        std::print("Upgrade cfire ({}) from {} to {}? [y/N]: ",
            layout.executable_path.string(), cur_semver->tag(), target_semver->tag());
        std::fflush(stdout);
        std::string ans;
        std::getline(std::cin, ans);
        auto a = trim_sv(ans);
        if (a != "y" && a != "Y" && a != "yes" && a != "YES") {
            std::println("Upgrade cancelled.");
            return 1;
        }
    }

    std::string eff_base = base_url.empty()
        ? std::format("https://github.com/gsandoval/cfire/releases/download/{}", target_semver->tag())
        : base_url;

    std::string bundle_name = std::format("cfire-{}-{}-{}", target_semver->tag(), platform.os, platform.arch);
    std::string archive_name = bundle_name + platform.archive_ext;
    std::string archive_url = std::format("{}/{}", eff_base, archive_name);
    std::string sums_url = std::format("{}/SHA256SUMS.txt", eff_base);

    cfire::detail::SecureTempDir stage("cfire_upgrade");
    if (!stage.ok()) {
        std::println(stderr, "error: failed to create secure temporary staging directory");
        return 1;
    }

    auto archive_path = stage.file(archive_name);
    auto sums_path = stage.file("SHA256SUMS.txt");

    std::println("==> Downloading {} and SHA256SUMS.txt...", archive_name);
    if (auto dl1 = download_release_file(archive_url, archive_path); !dl1) {
        std::println(stderr, "error: {}", dl1.error());
        return 1;
    }
    if (auto dl2 = download_release_file(sums_url, sums_path); !dl2) {
        std::println(stderr, "error: {}", dl2.error());
        return 1;
    }

    auto expected_sha = find_checksum_entry(sums_path, archive_name);
    if (!expected_sha) {
        std::println(stderr, "error: {}", expected_sha.error());
        return 1;
    }

    auto actual_sha = cfire::build::sha256_file_hex(archive_path);
    if (!actual_sha) {
        std::println(stderr, "error: {}", actual_sha.error());
        return 1;
    }
    if (*actual_sha != *expected_sha) {
        std::println(stderr, "error: SHA-256 verification failed for {}!\n  expected: {}\n  actual:   {}",
            archive_name, *expected_sha, *actual_sha);
        return 1;
    }
    std::println("  ✓ SHA-256 verified ({})", *actual_sha);

    auto bundle_root = unpack_release_archive(archive_path, stage.path() / "unpack", platform);
    if (!bundle_root) {
        std::println(stderr, "error: {}", bundle_root.error());
        return 1;
    }

    if (auto rt_swap = swap_runtime_dir_atomic(
            *bundle_root / "runtime",
            layout.runtime_dir,
            target_semver->str(),
            target_semver->tag()); !rt_swap) {
        std::println(stderr, "error: {}", rt_swap.error());
        return 1;
    }

    if (auto bin_swap = swap_binary_atomic(*bundle_root / platform.exe_name, layout.executable_path); !bin_swap) {
        std::println(stderr, "error: {}", bin_swap.error());
        return 1;
    }

    std::println("✓ Upgraded cfire {} -> {} ({})", cur_semver->tag(), target_semver->tag(), layout.executable_path.string());
    std::println("  Note: Caches are versioned automatically; run 'cfire clean --cache' only if you want to reclaim old cache space.");
    return 0;
}

} // namespace cfire::cli::upgrade
