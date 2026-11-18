#pragma once
// tests/unit/test_toolchain_upgrade.hpp — Tests for `cfire upgrade` (`self-update`) & SemVer / integrity checks

#include "../test_harness.hpp"
#include "../../src/cli/upgrade_command.hpp"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#if __has_include(<print>)
#include <print>
#endif
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace test_toolchain_upgrade_detail {

struct ScopedEnvVar {
    std::string key;
    bool had_old = false;
    std::string old_val;

    ScopedEnvVar(std::string k, const std::string& val) : key(std::move(k)) {
        if (const char* cur = std::getenv(key.c_str())) {
            had_old = true;
            old_val = cur;
        }
        cfire::build::set_process_env(key.c_str(), val);
    }

    explicit ScopedEnvVar(std::string k, std::nullptr_t) : key(std::move(k)) {
        if (const char* cur = std::getenv(key.c_str())) {
            had_old = true;
            old_val = cur;
        }
        cfire::build::unset_process_env(key.c_str());
    }

    ~ScopedEnvVar() {
        if (had_old) {
            cfire::build::set_process_env(key.c_str(), old_val);
        } else {
            cfire::build::unset_process_env(key.c_str());
        }
    }
};

[[nodiscard]] inline auto read_file_bytes_str(const std::filesystem::path& p) -> std::string {
    std::ifstream ifs(p, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) return {};
    return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

[[nodiscard]] inline auto make_file_url(const std::filesystem::path& dir) -> std::string {
#if defined(_WIN32)
    return "file:///" + dir.generic_string();
#else
    return "file://" + dir.string();
#endif
}

[[nodiscard]] inline auto run_exe(const std::filesystem::path& exe, std::string_view args) -> RunResult {
    RunResult r;
    r.output = cfire::bridge::run_command_capture(std::format("\"{}\" {}", exe.string(), args), r.exit_code);
    return r;
}

} // namespace test_toolchain_upgrade_detail

void test_toolchain_upgrade([[maybe_unused]] const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_toolchain_upgrade");
    using namespace test_toolchain_upgrade_detail;
    namespace upg = cfire::cli::upgrade;

    // 1. SemVer parser & comparator unit tests
    {
        auto v0 = upg::parse_semver("0.11.0");
        auto v1 = upg::parse_semver("v0.11.1");
        auto v2 = upg::parse_semver("0.12.0");
        auto v3 = upg::parse_semver("v1.0.0\n");
        require(v0 && v0->major == 0 && v0->minor == 11 && v0->patch == 0, "parse_semver 0.11.0");
        require(v1 && v1->tag() == "v0.11.1", "parse_semver v0.11.1");
        require(v2 && upg::compare_semver(*v0, *v1) < 0 && upg::compare_semver(*v1, *v2) < 0 && upg::compare_semver(*v2, *v3) < 0,
            "compare_semver orders 0.11.0 < 0.11.1 < 0.12.0 < 1.0.0");
        require(upg::compare_semver(*v0, *upg::parse_semver("v0.11.0")) == 0, "compare_semver equality");
        require(!upg::parse_semver("0.11").has_value(), "parse_semver rejects 2-component version");
        require(!upg::parse_semver("v1.2.3.4").has_value(), "parse_semver rejects 4-component version");
        require(!upg::parse_semver("v1.2.beta").has_value(), "parse_semver rejects non-numeric segment");
        require(!upg::parse_semver("").has_value(), "parse_semver rejects empty string");
        require(!upg::parse_semver("-1.0.0").has_value(), "parse_semver rejects negative version");

        require(!upg::validate_upgrade_arg("version", "--upload-pack=x").has_value(),
            "validate_upgrade_arg rejects option injection");
        require(!upg::validate_upgrade_arg("url", "ext::evil").has_value(),
            "validate_upgrade_arg rejects ext:: transport");
        require(!upg::validate_upgrade_arg("url", "fd::0").has_value(),
            "validate_upgrade_arg rejects fd:: transport");
        require(!upg::validate_upgrade_arg("version", "v1\n2").has_value(),
            "validate_upgrade_arg rejects control newline");
        require(upg::validate_upgrade_arg("version", "v0.12.0").has_value(),
            "validate_upgrade_arg accepts v0.12.0");

        auto p_unix = upg::file_url_to_path("file:///tmp/my%20dir/test");
        require(p_unix.has_value() && p_unix->generic_string().contains("my dir/test"),
            "file_url_to_path decodes percent-encoded file:// URL");

        auto plat = upg::detect_target_platform();
        require(plat.has_value() && !plat->os.empty() && !plat->arch.empty(),
            plat ? "detect_target_platform succeeds" : plat.error());
    }

    // 2. CLI --help, self-update alias, and source-workspace guard
    {
        run_cfire("upgrade --help")
            .assert_ok("cfire upgrade --help exits 0")
            .assert_contains("self-update", "cfire upgrade --help mentions self-update");
        run_cfire("self-update --help")
            .assert_ok("cfire self-update --help exits 0")
            .assert_contains("self-update", "cfire self-update --help prints upgrade usage");

        ScopedEnvVar clr_rt("CFIRE_RUNTIME_DIR", nullptr);
        auto ws_refusal = run_cfire("upgrade --version v0.12.0 --yes");
        ws_refusal.assert_fail("cfire upgrade refuses source workspace checkout without --force");
        ws_refusal.assert_contains("source workspace checkout", "workspace refusal gives clear remediation message");
    }

    // 3. Hermetic file:// end-to-end upgrade across PortableDir and SystemPrefix (including Windows self-rename)
    {
        auto plat_res = upg::detect_target_platform();
        require(plat_res.has_value(), "detect_target_platform for upgrade E2E");
        if (!plat_res) return;
        const auto& plat = *plat_res;

        ScopedTempDir tmp("toolchain_upgrade_e2e");
        std::error_code ec;

        auto mock_dir = tmp.path / "mock_release";
        std::filesystem::create_directories(mock_dir, ec);

        std::string bundle_name = std::format("cfire-v0.12.0-{}-{}", plat.os, plat.arch);
        std::string archive_name = bundle_name + plat.archive_ext;
        auto staged_bundle = mock_dir / bundle_name;

        run_cfire(std::format("install --portable \"{}\"", staged_bundle.string()))
            .assert_ok("stage mock v0.12.0 portable bundle");

        // Plant a marker header unique to the "v0.12.0" bundle so post-upgrade checks prove runtime swap worked.
        {
            std::ofstream mofs(staged_bundle / "runtime" / "cfire_upgrade_marker.hpp", std::ios::trunc);
            mofs << "// cfire v0.12.0 upgraded runtime marker\n";
        }

#if defined(_WIN32)
        int pkg_rc = 0;
        std::string pkg_out = cfire::bridge::run_command_capture(
            std::format("powershell -NoProfile -ExecutionPolicy Bypass -Command \"$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'; "
                        "Compress-Archive -Path {} -DestinationPath {} -Force\"",
                        upg::ps_single_quote(staged_bundle.string()),
                        upg::ps_single_quote((mock_dir / archive_name).string())),
            pkg_rc);
#else
        int pkg_rc = 0;
        std::string pkg_out = cfire::bridge::run_command_capture(
            std::format("tar -czf {} -C {} {}",
                        cfire::bridge::shell_quote((mock_dir / archive_name).string()),
                        cfire::bridge::shell_quote(mock_dir.string()),
                        cfire::bridge::shell_quote(bundle_name)),
            pkg_rc);
#endif
        require(pkg_rc == 0 && std::filesystem::is_regular_file(mock_dir / archive_name, ec),
            std::format("package mock upgrade archive failed (rc={}): {}", pkg_rc, pkg_out));

        std::string valid_sha = cfire::sha256::hash_hex(read_file_bytes_str(mock_dir / archive_name));
        require(valid_sha.size() == 64, "computed SHA-256 for mock upgrade archive");
        {
            std::ofstream sums(mock_dir / "SHA256SUMS.txt", std::ios::trunc);
            sums << valid_sha << "  " << archive_name << "\n";
            std::ofstream latest(mock_dir / "LATEST", std::ios::trunc);
            latest << "v0.12.0\n";
        }

        // 3a. PortableDir layout upgrade (--check, --yes, up-to-date no-op, --force, tamper abort)
        auto portable_dir = tmp.path / "portable_install";
        run_cfire(std::format("install --portable \"{}\"", portable_dir.string()))
            .assert_ok("seed initial PortableDir install");
        auto portable_exe = portable_dir / plat.exe_name;
        auto stale_rt_file = portable_dir / "runtime" / "stale_old_runtime_header.hpp";
        {
            std::ofstream sofs(stale_rt_file, std::ios::trunc);
            sofs << "// stale header before upgrade\n";
        }

        const std::string mock_url = make_file_url(mock_dir);
        {
            ScopedEnvVar clr_rt("CFIRE_RUNTIME_DIR", nullptr);
            ScopedEnvVar clr_ver("CFIRE_VERSION", nullptr);
            ScopedEnvVar clr_latest("CFIRE_LATEST_RELEASE_URL", nullptr);
            ScopedEnvVar env_base("CFIRE_RELEASE_BASE_URL", mock_url);
            ScopedEnvVar env_cur("CFIRE_UPGRADE_CURRENT_VERSION", "0.11.0");

            auto r_chk = run_exe(portable_exe, "upgrade --check");
            r_chk.assert_ok("PortableDir upgrade --check succeeds");
            r_chk.assert_contains("Update available: v0.11.0 -> v0.12.0", "--check reports available upgrade");
            require(std::filesystem::is_regular_file(stale_rt_file, ec),
                "--check does not modify installed runtime directory");

            auto r_up = run_exe(portable_exe, "upgrade --yes");
            r_up.assert_ok("PortableDir upgrade --yes succeeds");
            r_up.assert_contains("Upgraded cfire v0.11.0 -> v0.12.0", "upgrade reports v0.11.0 -> v0.12.0");
            require(std::filesystem::is_regular_file(portable_dir / "runtime" / "cfire_upgrade_marker.hpp", ec),
                "upgrade installs new runtime marker header");
            require(!std::filesystem::exists(stale_rt_file, ec),
                "upgrade prunes stale runtime header cleanly");

            run_exe(portable_exe, "--version")
                .assert_ok("upgraded PortableDir binary runs --version cleanly");
#if defined(_WIN32)
            require(!std::filesystem::exists(portable_dir / "cfire.exe.old", ec),
                "Windows startup cleanup deletes stale cfire.exe.old after next invocation");
#endif

            {
                ScopedEnvVar env_cur_same("CFIRE_UPGRADE_CURRENT_VERSION", "0.12.0");
                auto r_same = run_exe(portable_exe, "upgrade --yes");
                r_same.assert_ok("upgrade is a clean no-op when already at latest version");
                r_same.assert_contains("already up to date", "no-op reports already up to date");

                std::filesystem::remove(portable_dir / "runtime" / "cfire_upgrade_marker.hpp", ec);
                auto r_force = run_exe(portable_exe, "upgrade --yes --force");
                r_force.assert_ok("upgrade --force reinstalls even on matching version");
                require(std::filesystem::is_regular_file(portable_dir / "runtime" / "cfire_upgrade_marker.hpp", ec),
                    "upgrade --force restores runtime contents");
            }

            // 3b. Corrupted SHA256SUMS.txt aborts safely before mutating existing install
            {
                std::ofstream sums(mock_dir / "SHA256SUMS.txt", std::ios::trunc);
                sums << "0000000000000000000000000000000000000000000000000000000000000000  " << archive_name << "\n";
            }
            auto sentinel = portable_dir / "runtime" / "preserve_sentinel.hpp";
            {
                std::ofstream pofs(sentinel, std::ios::trunc);
                pofs << "// must survive rejected upgrade\n";
            }
            auto r_bad = run_exe(portable_exe, "upgrade --yes");
            r_bad.assert_fail("tampered SHA256SUMS.txt aborts upgrade");
            r_bad.assert_contains("SHA-256", "tampered upgrade prints SHA-256 error");
            require(std::filesystem::is_regular_file(sentinel, ec),
                "aborted upgrade preserves existing runtime files untouched");
            run_exe(portable_exe, "--version")
                .assert_ok("existing PortableDir binary remains runnable after aborted upgrade");
        }

        // Restore valid SHA256SUMS.txt for SystemPrefix test
        {
            std::ofstream sums(mock_dir / "SHA256SUMS.txt", std::ios::trunc);
            sums << valid_sha << "  " << archive_name << "\n";
        }

        // 3c. SystemPrefix layout + `self-update` alias + `--version`
        auto prefix_dir = tmp.path / "prefix_install";
        run_cfire(std::format("install --prefix \"{}\"", prefix_dir.string()))
            .assert_ok("seed SystemPrefix install");
        auto prefix_exe = prefix_dir / "bin" / plat.exe_name;
        auto prefix_rt = prefix_dir / "share" / "cfire" / "runtime";
        auto stale_prefix = prefix_rt / "stale_prefix_header.hpp";
        {
            std::ofstream sp(stale_prefix, std::ios::trunc);
            sp << "// stale prefix header\n";
        }
        {
            ScopedEnvVar clr_rt("CFIRE_RUNTIME_DIR", nullptr);
            ScopedEnvVar clr_ver("CFIRE_VERSION", nullptr);
            ScopedEnvVar clr_latest("CFIRE_LATEST_RELEASE_URL", nullptr);
            ScopedEnvVar env_base("CFIRE_RELEASE_BASE_URL", mock_url);
            ScopedEnvVar env_cur("CFIRE_UPGRADE_CURRENT_VERSION", "0.11.0");

            auto r_pref = run_exe(prefix_exe, "self-update --version v0.12.0 --yes");
            r_pref.assert_ok("SystemPrefix self-update --version v0.12.0 --yes succeeds");
            require(std::filesystem::is_regular_file(prefix_rt / "cfire_upgrade_marker.hpp", ec),
                "SystemPrefix self-update updates share/cfire/runtime");
            require(!std::filesystem::exists(stale_prefix, ec),
                "SystemPrefix self-update prunes stale runtime headers");
            run_exe(prefix_exe, "--version")
                .assert_ok("upgraded SystemPrefix binary runs --version cleanly");
        }
    }

    std::println("--- PASS: test_toolchain_upgrade");
}
