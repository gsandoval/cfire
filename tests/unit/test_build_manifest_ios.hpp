#pragma once
#include "../test_harness.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <optional>
#include <vector>
#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace test_build_manifest_ios_detail {

struct ScopedEnvGuard {
    std::string key;
    bool had_old = false;
    std::string old_val{};

    ScopedEnvGuard(std::string_view k, std::optional<std::string_view> new_val) : key(k) {
        if (const char* cur = std::getenv(key.c_str())) {
            had_old = true;
            old_val = cur;
        }
        apply(new_val);
    }

    ~ScopedEnvGuard() {
        if (had_old) {
            apply(std::string_view(old_val));
        } else {
            apply(std::nullopt);
        }
    }

    ScopedEnvGuard(const ScopedEnvGuard&) = delete;
    ScopedEnvGuard& operator=(const ScopedEnvGuard&) = delete;

private:
    void apply(std::optional<std::string_view> v) {
#if defined(_WIN32)
        std::string val = v ? std::string(*v) : std::string();
        (void)::_putenv_s(key.c_str(), val.c_str());
#else
        if (v) {
            (void)::setenv(key.c_str(), std::string(*v).c_str(), 1);
        } else {
            (void)::unsetenv(key.c_str());
        }
#endif
    }
};

inline void check_os_dirs_and_open_url() {
#if !defined(_WIN32)
    ScopedTempDir fake_home("cfire_os_home");
    ScopedEnvGuard home_guard("HOME", fake_home.path.string());
    ScopedEnvGuard xdg_data("XDG_DATA_HOME", std::nullopt);
    ScopedEnvGuard xdg_cache("XDG_CACHE_HOME", std::nullopt);
    ScopedEnvGuard xdg_docs("XDG_DOCUMENTS_DIR", std::nullopt);

    auto base_data = cfire::os::data_dir();
    auto base_cache = cfire::os::cache_dir();
    auto docs = cfire::os::documents_dir();
    require(base_data.has_value() && std::filesystem::is_directory(*base_data),
        "os.data_dir() creates base directory");
    require(base_cache.has_value() && std::filesystem::is_directory(*base_cache),
        "os.cache_dir() creates base directory");
    require(docs.has_value() && std::filesystem::is_directory(*docs),
        "os.documents_dir() creates documents directory");

    auto app_data = cfire::os::data_dir("demo-app");
    auto app_cache = cfire::os::cache_dir("demo-app");
    require(app_data.has_value() && std::filesystem::is_directory(*app_data),
        "os.data_dir('demo-app') creates app subdirectory");
    require(app_cache.has_value() && std::filesystem::is_directory(*app_cache),
        "os.cache_dir('demo-app') creates app subdirectory");

    if (app_data) {
        struct ::stat st{};
        require(::stat(app_data->c_str(), &st) == 0 && (st.st_mode & 0777) == 0700,
            "os.data_dir sets 0700 mode on newly created directory");
    }
    if (app_cache) {
        struct ::stat st{};
        require(::stat(app_cache->c_str(), &st) == 0 && (st.st_mode & 0777) == 0700,
            "os.cache_dir sets 0700 mode on newly created directory");
    }

#if defined(__APPLE__)
    require(app_data && app_data->ends_with("Library/Application Support/demo-app"),
        "macOS/iOS data_dir uses Library/Application Support");
    require(app_cache && app_cache->ends_with("Library/Caches/demo-app"),
        "macOS/iOS cache_dir uses Library/Caches");
    std::filesystem::path preexist_dir = fake_home.path / "Library" / "Caches" / "keep755";
#else
    require(app_data && app_data->ends_with(".local/share/demo-app"),
        "Linux data_dir uses .local/share");
    require(app_cache && app_cache->ends_with(".cache/demo-app"),
        "Linux cache_dir uses .cache");
    std::filesystem::path preexist_dir = fake_home.path / ".cache" / "keep755";

    auto custom_data = fake_home.subpath("xdg_data");
    auto custom_cache = fake_home.subpath("xdg_cache");
    auto custom_docs = fake_home.subpath("xdg_docs");
    ScopedEnvGuard xdg_d("XDG_DATA_HOME", custom_data.string());
    ScopedEnvGuard xdg_c("XDG_CACHE_HOME", custom_cache.string());
    ScopedEnvGuard xdg_doc("XDG_DOCUMENTS_DIR", custom_docs.string());
    auto xd = cfire::os::data_dir("xdg_app");
    auto xc = cfire::os::cache_dir("xdg_app");
    auto xdocs = cfire::os::documents_dir();
    require(xd.has_value() && xd->starts_with(custom_data.string()) && xd->ends_with("xdg_app"),
        "Linux data_dir honors XDG_DATA_HOME");
    require(xc.has_value() && xc->starts_with(custom_cache.string()) && xc->ends_with("xdg_app"),
        "Linux cache_dir honors XDG_CACHE_HOME");
    require(xdocs.has_value() && *xdocs == custom_docs.string(),
        "Linux documents_dir honors XDG_DOCUMENTS_DIR");
#endif

    {
        std::error_code ec;
        std::filesystem::create_directories(preexist_dir, ec);
        (void)::chmod(preexist_dir.c_str(), 0755);
        auto kept = cfire::os::cache_dir("keep755");
        struct ::stat st{};
        require(kept.has_value() && ::stat(preexist_dir.c_str(), &st) == 0 && (st.st_mode & 0777) == 0755,
            "cache_dir preserves permissions on pre-existing directory");
    }
#else
    std::string pid_app = std::format("cfire_ios_unit_{}", cfire::os::getpid());
    auto app_data = cfire::os::data_dir(pid_app);
    auto d_clean = cfire::scope_exit([&] {
        if (app_data) {
            std::error_code ec;
            std::filesystem::remove_all(*app_data, ec);
        }
    });
    auto app_cache = cfire::os::cache_dir(pid_app);
    auto c_clean = cfire::scope_exit([&] {
        if (app_cache) {
            std::error_code ec;
            std::filesystem::remove_all(*app_cache, ec);
        }
    });
    auto docs = cfire::os::documents_dir();
    require(app_data.has_value() && !app_data->empty() && std::filesystem::is_directory(*app_data),
        "Windows os.data_dir creates folder");
    require(app_cache.has_value() && !app_cache->empty() && std::filesystem::is_directory(*app_cache),
        "Windows os.cache_dir creates folder");
    require(docs.has_value() && !docs->empty() && std::filesystem::is_directory(*docs),
        "Windows os.documents_dir resolves Documents");
#endif

    for (std::string_view bad_app : {
             std::string_view("../evil"),
             std::string_view(".."),
             std::string_view("."),
             std::string_view("a/b"),
             std::string_view("a\\b"),
             std::string_view("bad:name"),
             std::string_view("a\0b", 3),
         }) {
        require(!cfire::os::data_dir(bad_app).has_value(), "os.data_dir rejects traversal/invalid app name");
        require(!cfire::os::cache_dir(bad_app).has_value(), "os.cache_dir rejects traversal/invalid app name");
    }

    for (std::string_view bad_url : {
             std::string_view(""),
             std::string_view("javascript:alert(1)"),
             std::string_view("file:///etc/passwd"),
             std::string_view("https://example.com/\n"),
             std::string_view("https://example.com/\x7f"),
             std::string_view("not_a_url"),
             std::string_view("mailto:"),
             std::string_view("https://"),
         }) {
        require(!cfire::os::open_url(bad_url).has_value(), "os.open_url rejects unsafe/malformed URL");
    }

    ScopedEnvGuard headless_guard("CFIRE_HEADLESS_TEST", "1");
    cfire::os::detail::clear_last_opened_url();
    auto opened = cfire::os::open_url("https://cfire.memo.engineer/docs");
    require(opened.has_value(), "os.open_url succeeds under CFIRE_HEADLESS_TEST=1");
    require(cfire::os::detail::last_opened_url() == "https://cfire.memo.engineer/docs",
        "os.open_url records URL in headless slot");
    cfire::os::detail::clear_last_opened_url();
}

inline void check_keychain_biometric() {
    auto empty_reason = cfire::keychain::authenticate_biometric("");
    require(!empty_reason.has_value(), "keychain.authenticate_biometric rejects empty reason");

    {
        ScopedEnvGuard headless_guard("CFIRE_HEADLESS_TEST", "1");
        ScopedEnvGuard mock_env_guard("CFIRE_MOCK_BIOMETRIC", std::nullopt);
        auto bio_clean = cfire::scope_exit([] { cfire::keychain::detail::clear_mock_biometric(); });

        cfire::keychain::detail::clear_mock_biometric();
        auto r_default = cfire::keychain::authenticate_biometric("Unlock vault");
        require(r_default.has_value() && *r_default == true,
            "headless biometric defaults to Ok(true)");

        cfire::keychain::detail::set_mock_biometric("0");
        auto r_false = cfire::keychain::authenticate_biometric("Unlock vault");
        require(r_false.has_value() && *r_false == false,
            "set_mock_biometric('0') returns Ok(false)");

        cfire::keychain::detail::set_mock_biometric("1");
        auto r_true = cfire::keychain::authenticate_biometric("Unlock vault", false);
        require(r_true.has_value() && *r_true == true,
            "set_mock_biometric('1') returns Ok(true)");

        cfire::keychain::detail::set_mock_biometric("err");
        auto r_err = cfire::keychain::authenticate_biometric("Unlock vault");
        require(!r_err.has_value(), "set_mock_biometric('err') returns Err");

        cfire::keychain::detail::clear_mock_biometric();
        {
            ScopedEnvGuard mock0("CFIRE_MOCK_BIOMETRIC", "0");
            auto r_env0 = cfire::keychain::authenticate_biometric("Unlock vault");
            require(r_env0.has_value() && *r_env0 == false,
                "CFIRE_MOCK_BIOMETRIC=0 returns Ok(false)");
        }
        {
            ScopedEnvGuard mock_err("CFIRE_MOCK_BIOMETRIC", "err");
            auto r_enverr = cfire::keychain::authenticate_biometric("Unlock vault");
            require(!r_enverr.has_value(), "CFIRE_MOCK_BIOMETRIC=err returns Err");
        }
    }

#if !defined(__APPLE__)
    ScopedEnvGuard no_hl("CFIRE_HEADLESS_TEST", "0");
    auto r_unsupported = cfire::keychain::authenticate_biometric("Check fallback");
    require(!r_unsupported.has_value() && r_unsupported.error().message().contains("not supported"),
        "non-Apple authenticate_biometric returns clean not-supported Err");
#endif
}

inline void check_build_and_link_smoke([[maybe_unused]] const std::filesystem::path& rt_dir) {
#if defined(__APPLE__) || defined(_WIN32)
    ScopedTempDir tmp("cfire_ios_unit_build");
    auto cache_dir = tmp.subpath("cache");
    ScopedEnvGuard cache_guard("CFIRE_CACHE_DIR", cache_dir.string());

    auto has_ldflag = [](const cfire::build::BuildPlan& plan, std::string_view flag) -> bool {
        return std::ranges::find(plan.ldflags, flag) != plan.ldflags.end();
    };
#endif

#if defined(__APPLE__)
    {
        auto cli_cf = tmp.file("cli_smoke.cf", R"CF(package main

import "core:keychain"
import "core:os"

fn main() {
    _ = os.data_dir("cfire_cli_smoke") or { return }
    _ = keychain.authenticate_biometric("CLI check", allow_passcode: false) or { return }
}
)CF");
        auto out_cli = tmp.path / "cli_smoke";
        auto plan = cfire::build::build_native(cli_cf, rt_dir, out_cli);
        require(plan.has_value(),
            std::format("macOS pure-C++ CLI build_native failed: {}", plan ? "" : plan.error()));
        if (plan) {
            require(!plan->uses_objc, "pure-C++ CLI build must not force uses_objc");
            require(has_ldflag(*plan, "-lobjc"), "core:keychain links -lobjc on macOS");
            require(std::filesystem::exists(out_cli), "macOS CLI binary exists");
        }
    }

    {
        auto arc_cf = tmp.file("arc_smoke.cf", R"CF(package main

import "ext:webui"
import "core:keychain"
import "core:os"

fn main() {
    mut app := make(webui.App, "ArcCheck", 640, 480)
    defer app.close()
    _ = os.data_dir("cfire_arc") or { return }
    _ = keychain.authenticate_biometric("arc check") or { return }
}
)CF");
        auto out_arc = tmp.path / "arc_smoke";
        auto plan = cfire::build::build_native(arc_cf, rt_dir, out_arc);
        require(plan.has_value(),
            std::format("macOS ObjC++ (-fobjc-arc) build_native failed: {}", plan ? "" : plan.error()));
        if (plan) {
            require(plan->uses_objc, "ext:webui enables uses_objc (-fobjc-arc)");
            require(std::filesystem::exists(out_arc), "macOS ARC binary exists");
        }
    }

    if (auto sdk = cfire::build::resolve_ios_sdk_path(true); sdk.has_value()) {
        auto ios_cf = tmp.file("ios_smoke.cf", R"CF(package main

import "core:keychain"
import "core:os"

fn main() {
    _ = os.data_dir("cfire_ios") or { return }
    _ = os.cache_dir("cfire_ios") or { return }
    _ = os.documents_dir() or { return }
    os.open_url("https://cfire.memo.engineer") or { return }
    _ = keychain.authenticate_biometric("Unlock vault") or { return }
}
)CF");
        auto out_ios = tmp.path / "ios_smoke";
        auto plan = cfire::build::build_native(ios_cf, rt_dir, out_ios, false, false, "ios-sim");
        require(plan.has_value(),
            std::format("ios-sim build_native failed: {}", plan ? "" : plan.error()));
        if (plan) {
            require(plan->is_ios && plan->is_ios_sim, "ios-sim target flags set on BuildPlan");
            require(has_ldflag(*plan, "-lobjc"), "iOS plan links -lobjc");
            require(has_ldflag(*plan, "Foundation"), "iOS plan links Foundation");
            require(has_ldflag(*plan, "UIKit"), "iOS plan links UIKit");
            require(has_ldflag(*plan, "Security"), "iOS plan links Security");
            require(has_ldflag(*plan, "CoreFoundation"), "iOS plan links CoreFoundation");
            require(std::filesystem::exists(out_ios), "ios-sim binary exists");
        }
    }
#elif defined(_WIN32)
    {
        auto win_cf = tmp.file("win_smoke.cf", R"CF(package main

import "core:keychain"
import "core:os"

fn main() {
    _ = os.data_dir("cfire_win") or { return }
    _ = os.cache_dir("cfire_win") or { return }
    _ = os.documents_dir() or { return }
    _ = keychain.authenticate_biometric("Windows check") or { return }
}
)CF");
        auto out_win = tmp.path / "win_smoke.exe";
        auto plan = cfire::build::build_native(win_cf, rt_dir, out_win);
        require(plan.has_value(),
            std::format("Windows build_native failed: {}", plan ? "" : plan.error()));
        if (plan) {
            require(has_ldflag(*plan, "-lshell32"), "Windows plan links -lshell32");
            require(has_ldflag(*plan, "-lole32"), "Windows plan links -lole32");
            require(has_ldflag(*plan, "-luuid"), "Windows plan links -luuid");
            require(has_ldflag(*plan, "-ladvapi32"), "Windows plan links -ladvapi32");
            require(std::filesystem::exists(out_win), "Windows binary exists");
        }
    }
#endif
}

struct ScopedBuildGlobalsGuard {
    cfire::build::BuildProfile prev_profile;
    std::string prev_macos_min;
    std::string prev_ios_min;
    std::string prev_sign;
    std::string prev_prof;
    std::string prev_ent;
    bool prev_ipa;

    ScopedBuildGlobalsGuard()
        : prev_profile(cfire::build::current_build_profile()),
          prev_macos_min(cfire::build::g_macos_min_version),
          prev_ios_min(cfire::build::g_ios_min_version),
          prev_sign(cfire::build::g_sign_identity_override),
          prev_prof(cfire::build::g_provisioning_profile_override),
          prev_ent(cfire::build::g_entitlements_override),
          prev_ipa(cfire::build::g_emit_ipa_mode) {}

    ~ScopedBuildGlobalsGuard() {
        cfire::build::set_build_profile(prev_profile);
        cfire::build::g_macos_min_version = prev_macos_min;
        cfire::build::g_ios_min_version = prev_ios_min;
        cfire::build::g_sign_identity_override = prev_sign;
        cfire::build::g_provisioning_profile_override = prev_prof;
        cfire::build::g_entitlements_override = prev_ent;
        cfire::build::g_emit_ipa_mode = prev_ipa;
    }

    ScopedBuildGlobalsGuard(const ScopedBuildGlobalsGuard&) = delete;
    ScopedBuildGlobalsGuard& operator=(const ScopedBuildGlobalsGuard&) = delete;
};

[[nodiscard]] inline auto count_substr(std::string_view hay, std::string_view needle) -> std::size_t {
    if (needle.empty()) return 0;
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string_view::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

inline void write_valid_rgba_png(const std::filesystem::path& path, uint32_t w, uint32_t h) {
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(h) * (1 + static_cast<std::size_t>(w) * 4));
    for (uint32_t y = 0; y < h; ++y) {
        raw.push_back(0); // filter type 0
        for (uint32_t x = 0; x < w; ++x) {
            raw.push_back(0x28);
            raw.push_back(0x88);
            raw.push_back(0xE6);
            raw.push_back(0xFF);
        }
    }

    // Stored zlib stream (RFC 1950 + RFC 1951 uncompressed BFINAL=1 block)
    std::vector<uint8_t> zlib_data;
    zlib_data.push_back(0x78);
    zlib_data.push_back(0x01);
    std::size_t off = 0;
    while (off < raw.size()) {
        const std::size_t chunk = std::min<std::size_t>(65535, raw.size() - off);
        const bool is_last = (off + chunk == raw.size());
        zlib_data.push_back(is_last ? 0x01 : 0x00);
        const uint16_t len16 = static_cast<uint16_t>(chunk);
        const uint16_t nlen16 = static_cast<uint16_t>(~len16);
        zlib_data.push_back(static_cast<uint8_t>(len16 & 0xFF));
        zlib_data.push_back(static_cast<uint8_t>(len16 >> 8));
        zlib_data.push_back(static_cast<uint8_t>(nlen16 & 0xFF));
        zlib_data.push_back(static_cast<uint8_t>(nlen16 >> 8));
        zlib_data.insert(zlib_data.end(), raw.begin() + static_cast<std::ptrdiff_t>(off),
            raw.begin() + static_cast<std::ptrdiff_t>(off + chunk));
        off += chunk;
    }
    uint32_t a = 1;
    uint32_t b = 0;
    for (uint8_t v : raw) {
        a = (a + v) % 65521u;
        b = (b + a) % 65521u;
    }
    const uint32_t adler = (b << 16) | a;
    zlib_data.push_back(static_cast<uint8_t>((adler >> 24) & 0xFF));
    zlib_data.push_back(static_cast<uint8_t>((adler >> 16) & 0xFF));
    zlib_data.push_back(static_cast<uint8_t>((adler >> 8) & 0xFF));
    zlib_data.push_back(static_cast<uint8_t>(adler & 0xFF));

    auto crc32 = [](const uint8_t* data, std::size_t len) -> uint32_t {
        uint32_t crc = 0xFFFFFFFFu;
        for (std::size_t i = 0; i < len; ++i) {
            crc ^= data[i];
            for (int k = 0; k < 8; ++k) {
                crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
            }
        }
        return ~crc;
    };

    std::vector<uint8_t> out{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    auto append_be32 = [&](uint32_t v) {
        out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(v & 0xFF));
    };
    auto append_chunk = [&](const char tag[4], const std::vector<uint8_t>& body) {
        append_be32(static_cast<uint32_t>(body.size()));
        const std::size_t tag_pos = out.size();
        out.insert(out.end(), tag, tag + 4);
        out.insert(out.end(), body.begin(), body.end());
        const uint32_t c = crc32(out.data() + tag_pos, 4 + body.size());
        append_be32(c);
    };

    std::vector<uint8_t> ihdr{
        static_cast<uint8_t>((w >> 24) & 0xFF), static_cast<uint8_t>((w >> 16) & 0xFF),
        static_cast<uint8_t>((w >> 8) & 0xFF), static_cast<uint8_t>(w & 0xFF),
        static_cast<uint8_t>((h >> 24) & 0xFF), static_cast<uint8_t>((h >> 16) & 0xFF),
        static_cast<uint8_t>((h >> 8) & 0xFF), static_cast<uint8_t>(h & 0xFF),
        8, 6, 0, 0, 0
    };
    append_chunk("IHDR", ihdr);
    append_chunk("IDAT", zlib_data);
    append_chunk("IEND", {});

    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
}

inline void check_packaging_manifest_and_helpers([[maybe_unused]] const std::filesystem::path& rt_dir) {
    ScopedTempDir tmp("cfire_pkg_meta_unit");
    auto bcf = tmp.file("full_manifest/build.cf", R"CF(package main
import "core:build"

app := build.Executable{
    Name: "demo",
    Entry: "main.cf",
    AppName: "Demo & <App>",
    BundleId: "com.example.demo",
    Version: "2.4.1",
    BuildNumber: "42",
    Resources: ["assets", "config.json"],
    IosMin: "16.4",
    IosOrientations: ["UIInterfaceOrientationPortrait", "UIInterfaceOrientationLandscapeRight"],
    IosDeviceFamily: [1],
    ProvisioningProfile: "dev.mobileprovision",
    Entitlements: "app.entitlements",
    PrivacyManifest: "CustomPrivacy.xcprivacy",
    SignIdentity: "Apple Development: Test",
    InfoPlist: {
        "NSCameraUsageDescription": "Scan cards & <QR>",
        "UIStatusBarHidden": true,
        "ITSAppUsesNonExemptEncryption": false
    }
}
)CF");

    auto parsed = cfire::build::parse_build_cf(bcf);
    require(parsed.has_value() && parsed->executables.size() == 1, "parse_build_cf parses extended packaging fields");
    if (parsed && !parsed->executables.empty()) {
        const auto& ex = parsed->executables[0];
        require(ex.version == "2.4.1", "Executable.Version parsed");
        require(ex.build_number == "42", "Executable.BuildNumber parsed");
        require(ex.has_explicit_resources && ex.resources.size() == 2 &&
                ex.resources[0] == "assets" && ex.resources[1] == "config.json",
            "Executable.Resources parsed");
        require(ex.ios_min == "16.4", "Executable.IosMin parsed");
        require(ex.ios_orientations.size() == 2 &&
                ex.ios_orientations[0] == "UIInterfaceOrientationPortrait" &&
                ex.ios_orientations[1] == "UIInterfaceOrientationLandscapeRight",
            "Executable.IosOrientations parsed");
        require(ex.ios_device_family.size() == 1 && ex.ios_device_family[0] == 1,
            "Executable.IosDeviceFamily parsed");
        require(ex.provisioning_profile == "dev.mobileprovision", "Executable.ProvisioningProfile parsed");
        require(ex.entitlements == "app.entitlements", "Executable.Entitlements parsed");
        require(ex.privacy_manifest == "CustomPrivacy.xcprivacy", "Executable.PrivacyManifest parsed");
        require(ex.sign_identity == "Apple Development: Test", "Executable.SignIdentity parsed");
        require(ex.info_plist_extras.size() == 3, "Executable.InfoPlist entries parsed");
        require(ex.info_plist_bool_keys.contains("UIStatusBarHidden") &&
                ex.info_plist_bool_keys.contains("ITSAppUsesNonExemptEncryption") &&
                !ex.info_plist_bool_keys.contains("NSCameraUsageDescription"),
            "Executable.InfoPlist tracks boolean vs string literals");
    }

    // Empty explicit resources distinguishes `Resources: []` from omitted
    auto empty_res_bcf = tmp.file("empty_res/build.cf", R"CF(package main
import "core:build"

app := build.Executable{
    Name: "emptyres",
    Entry: "main.cf",
    Resources: []
}
)CF");
    auto parsed_empty = cfire::build::parse_build_cf(empty_res_bcf);
    require(parsed_empty.has_value() && parsed_empty->executables.size() == 1 &&
            parsed_empty->executables[0].has_explicit_resources &&
            parsed_empty->executables[0].resources.empty(),
        "empty Resources: [] marks has_explicit_resources without copying ui/");

    // iOS deployment target helpers & dynamic triples
    require(!cfire::build::is_ios_min_supported("16.2"), "is_ios_min_supported rejects 16.2");
    require(!cfire::build::is_ios_min_supported("15.0"), "is_ios_min_supported rejects 15.0");
    require(!cfire::build::is_ios_min_supported("invalid"), "is_ios_min_supported rejects non-numeric");
    require(cfire::build::is_ios_min_supported("16.3"), "is_ios_min_supported accepts 16.3");
    require(cfire::build::is_ios_min_supported("17.0"), "is_ios_min_supported accepts 17.0");
    require(cfire::build::is_ios_min_supported("18.5"), "is_ios_min_supported accepts 18.5");
    require(cfire::build::normalize_ios_triple("ios", "16.4") == "arm64-apple-ios16.4",
        "normalize_ios_triple respects custom device ios_min");
    require(cfire::build::normalize_ios_triple("ios-sim", "17.5") == "arm64-apple-ios17.5-simulator",
        "normalize_ios_triple respects custom simulator ios_min");

    // Info.plist XML escaping, DT*, PrivacyInfo, and non-duplicate overrides
    cfire::build::BuildPlan pplan;
    pplan.app_name = "Demo & <App>";
    pplan.bundle_id = "com.example.demo";
    pplan.version = "2.4.1";
    pplan.build_number = "42";
    pplan.ios_min = "16.4";
    pplan.macos_min = "14.2";
    pplan.is_ios = true;
    pplan.is_ios_sim = true;
    pplan.ios_device_family = {1};
    pplan.ios_orientations = {"UIInterfaceOrientationPortrait"};
    pplan.info_plist_extras = {
        {"CFBundleDisplayName", "Custom Display & Co"},
        {"UILaunchScreen", "Main"},
        {"UIStatusBarHidden", "true"},
        {"ITSAppUsesNonExemptEncryption", "false"},
        {"NSHighResolutionCapable", "false"},
    };
    pplan.info_plist_bool_keys.insert("UIStatusBarHidden");
    pplan.info_plist_bool_keys.insert("ITSAppUsesNonExemptEncryption");
    pplan.info_plist_bool_keys.insert("NSHighResolutionCapable");

    cfire::build::AppleDtMetadata dt;
    dt.platform_name = "iphonesimulator";
    dt.platform_version = "27.2";
    dt.platform_build = "24B8118";
    dt.sdk_name = "iphonesimulator27.2";
    dt.sdk_build = "24B8118";
    dt.xcode_version = "2720";
    dt.xcode_build = "27B52";

    const std::string ios_plist = cfire::build::generate_ios_info_plist(
        pplan, "demo", dt, "    <key>CFBundleIconName</key>\n    <string>AppIcon</string>\n", false);
    require(ios_plist.contains("Demo &amp; &lt;App&gt;"), "iOS Info.plist XML-escapes app name");
    require(ios_plist.contains("Custom Display &amp; Co"), "iOS Info.plist XML-escapes InfoPlist string extras");
    require(count_substr(ios_plist, "<key>CFBundleDisplayName</key>") == 1,
        "iOS Info.plist avoids duplicate CFBundleDisplayName key when overridden");
    require(count_substr(ios_plist, "<key>UILaunchScreen</key>") == 1,
        "iOS Info.plist avoids duplicate UILaunchScreen key when overridden");
    require(ios_plist.contains("<key>UIApplicationSceneManifest</key>") &&
            ios_plist.contains("<string>CfireWebUISceneDelegate</string>"),
        "iOS Info.plist emits UIApplicationSceneManifest targeting CfireWebUISceneDelegate");
    require(ios_plist.contains("<key>DTXcode</key>\n    <string>2720</string>") &&
            ios_plist.contains("<key>DTSDKName</key>\n    <string>iphonesimulator27.2</string>"),
        "iOS Info.plist emits DT* SDK/Xcode metadata");
    require(ios_plist.contains("<key>MinimumOSVersion</key>\n    <string>16.4</string>"),
        "iOS Info.plist emits MinimumOSVersion");
    require(ios_plist.contains("<key>CFBundleShortVersionString</key>\n    <string>2.4.1</string>") &&
            ios_plist.contains("<key>CFBundleVersion</key>\n    <string>42</string>"),
        "iOS Info.plist emits Version and BuildNumber");
    require(ios_plist.contains("<key>UIDeviceFamily</key>\n    <array>\n        <integer>1</integer>\n    </array>"),
        "iOS Info.plist emits custom UIDeviceFamily");
    require(ios_plist.contains("<key>UIStatusBarHidden</key>\n    <true/>") &&
            ios_plist.contains("<key>ITSAppUsesNonExemptEncryption</key>\n    <false/>"),
        "iOS Info.plist emits boolean <true/>/<false/> extras");

    const std::string mac_plist = cfire::build::generate_macos_info_plist(pplan, "demo", true);
    require(mac_plist.contains("<key>LSMinimumSystemVersion</key>\n    <string>14.2</string>"),
        "macOS Info.plist emits LSMinimumSystemVersion");
    require(count_substr(mac_plist, "<key>NSHighResolutionCapable</key>") == 1,
        "macOS Info.plist avoids duplicate NSHighResolutionCapable key");

    const std::string priv_xml = cfire::build::generate_default_privacy_manifest_xml();
    require(priv_xml.contains("<key>NSPrivacyTracking</key>") &&
            priv_xml.contains("<key>NSPrivacyAccessedAPITypes</key>") &&
            priv_xml.contains("C617.1") && priv_xml.contains("35F9.1"),
        "PrivacyInfo.xcprivacy generator emits required privacy API declarations");

    const std::string rc = cfire::build::generate_windows_rc_content("", "2.4.1", "42", "Demo \"App\"");
    require(rc.contains("1 VERSIONINFO") &&
            rc.contains("FILEVERSION 2,4,1,42") &&
            rc.contains("PRODUCTVERSION 2,4,1,42") &&
            rc.contains("FILEOS 0x40004L") &&
            rc.contains("FILETYPE 0x1L") &&
            rc.contains("BLOCK \"040904B0\"") &&
            rc.contains("\"FileVersion\", \"2.4.1.42\"") &&
            rc.contains("\"ProductVersion\", \"2.4.1\"") &&
            rc.contains("Demo \"\"App\"\""),
        "Windows VS_VERSION_INFO RC generator emits valid version/string info");

    // CF-250 devicectl JSON parsing & selection
    const std::string single_dev_json = R"JSON({
      "result": {
        "devices": [
          {
            "identifier": "id-disc-1",
            "hardwareProperties": { "udid": "00008110-DISC" },
            "deviceProperties": { "name": "Old iPad" },
            "connectionProperties": { "tunnelState": "disconnected" }
          },
          {
            "identifier": "id-conn-1",
            "hardwareProperties": { "udid": "00008130-CONN" },
            "deviceProperties": { "name": "Memo's iPhone" },
            "connectionProperties": { "tunnelState": "connected" }
          }
        ]
      }
    })JSON";
    auto d_single = cfire::build::parse_devicectl_devices_json(single_dev_json);
    require(d_single.has_value() && d_single->udid == "00008130-CONN" && d_single->name == "Memo's iPhone",
        "parse_devicectl_devices_json picks sole connected device");

    const std::string two_devs_json = R"JSON({
      "result": {
        "devices": [
          {
            "identifier": "id-1",
            "hardwareProperties": { "udid": "UDID-AAA" },
            "deviceProperties": { "name": "Dev iPhone" },
            "connectionProperties": { "tunnelState": "connected" }
          },
          {
            "identifier": "id-2",
            "hardwareProperties": { "udid": "UDID-BBB" },
            "deviceProperties": { "name": "QA iPad" },
            "connectionProperties": { "tunnelState": "available" }
          }
        ]
      }
    })JSON";
    auto d_ambig = cfire::build::parse_devicectl_devices_json(two_devs_json, "");
    require(!d_ambig.has_value() && d_ambig.error().contains("--device"),
        "parse_devicectl_devices_json rejects ambiguous multiple devices without --device");
    auto d_ci = cfire::build::parse_devicectl_devices_json(two_devs_json, "qa ipad");
    require(d_ci.has_value() && d_ci->udid == "UDID-BBB",
        "parse_devicectl_devices_json matches device name case-insensitively");
    auto d_udid = cfire::build::parse_devicectl_devices_json(two_devs_json, "UDID-AAA");
    require(d_udid.has_value() && d_udid->identifier == "id-1",
        "parse_devicectl_devices_json matches device by UDID");
    auto d_missing = cfire::build::parse_devicectl_devices_json(two_devs_json, "NopePhone");
    require(!d_missing.has_value() && d_missing.error().contains("Dev iPhone"),
        "parse_devicectl_devices_json reports connected devices on mismatch");
    auto d_err_env = cfire::build::parse_devicectl_devices_json(
        R"({"error":{"userInfo":{"NSLocalizedDescription":{"string":"CoreDevice unavailable"}}}})");
    require(!d_err_env.has_value() && d_err_env.error().contains("CoreDevice unavailable"),
        "parse_devicectl_devices_json surfaces devicectl JSON error envelopes");

#if defined(__APPLE__)
    if (cfire::build::resolve_ios_sdk_path(true).has_value()) {
        auto kc_dir = tmp.subpath("kc_warn_proj");
        (void)tmp.file("kc_warn_proj/build.cf", R"CF(package main
import "core:build"

app := build.Executable{
    Name: "kcapp",
    Entry: "main.cf"
}
)CF");
        auto kc_cf = tmp.file("kc_warn_proj/main.cf", R"CF(package main
import "core:keychain"
fn main() {
    _ = keychain.authenticate_biometric("Unlock") or { return }
}
)CF");
        auto kc_plan = cfire::build::analyze_and_emit(kc_cf, rt_dir, false, false, "ios-sim", "", "auto", "auto", false, {}, {}, {}, {});
        require(kc_plan.has_value(), "analyze_and_emit succeeds for core:keychain on ios-sim");
        if (kc_plan) {
            bool has_faceid_warn = std::any_of(kc_plan->second.warnings.begin(), kc_plan->second.warnings.end(),
                [](const std::string& w) { return w.contains("NSFaceIDUsageDescription"); });
            require(has_faceid_warn, "core:keychain on iOS warns when NSFaceIDUsageDescription is unset");
        }

        (void)tmp.file("kc_ok_proj/build.cf", R"CF(package main
import "core:build"

app := build.Executable{
    Name: "kcok",
    Entry: "main.cf",
    InfoPlist: { "NSFaceIDUsageDescription": "Authenticate user" }
}
)CF");
        auto kc_ok_cf = tmp.file("kc_ok_proj/main.cf", R"CF(package main
import "core:keychain"
fn main() {
    _ = keychain.authenticate_biometric("Unlock") or { return }
}
)CF");
        auto kc_ok_plan = cfire::build::analyze_and_emit(kc_ok_cf, rt_dir, false, false, "ios-sim", "", "auto", "auto", false, {}, {}, {}, {});
        if (kc_ok_plan) {
            bool has_faceid_warn = std::any_of(kc_ok_plan->second.warnings.begin(), kc_ok_plan->second.warnings.end(),
                [](const std::string& w) { return w.contains("NSFaceIDUsageDescription"); });
            require(!has_faceid_warn, "core:keychain suppresses NSFaceIDUsageDescription warning when InfoPlist sets it");
        }

        auto web_cf = tmp.file("web_fw_proj/main.cf", R"CF(package main
import "ext:webui"
fn main() {
    mut app := make(webui.App, "FWCheck", 400, 300)
    defer app.close()
}
)CF");
        auto web_plan = cfire::build::analyze_and_emit(web_cf, rt_dir, false, false, "ios-sim", "", "auto", "auto", false, {}, {}, {}, {});
        require(web_plan.has_value(), "analyze_and_emit succeeds for ext:webui on ios-sim");
        if (web_plan) {
            auto has_fw = [&](std::string_view fw) {
                return std::ranges::find(web_plan->second.ldflags, fw) != web_plan->second.ldflags.end();
            };
            require(has_fw("SafariServices") && has_fw("PhotosUI") &&
                    has_fw("UniformTypeIdentifiers") && has_fw("UserNotifications"),
                "ext:webui iOS links SafariServices, PhotosUI, UniformTypeIdentifiers, UserNotifications");
            require(!has_fw("AVFoundation") && !has_fw("Photos"),
                "ext:webui iOS excludes AVFoundation and Photos (ITMS-90683)");
        }
    }
#endif
}

inline void check_cli_packaging_flags() {
    std::error_code ec;
#if defined(_WIN32)
    const bool has_cli = std::filesystem::exists("cfire.exe", ec);
#else
    const bool has_cli = std::filesystem::exists("cfire", ec);
#endif
    if (!has_cli) return;

    ScopedTempDir tmp("cfire_cli_pkg_flags");
    auto cf = tmp.file("hello.cf", "package main\nfn main() {\n    println(\"ok\")\n}\n");

    auto r_imin = run_cfire(std::format("build \"{}\" --ios-min=15.0 -o \"{}\" 2>&1",
        cf.string(), (tmp.path / "out_imin").string()));
    require(r_imin.exit_code == 2 && r_imin.output.contains("16.3"),
        std::format("--ios-min=15.0 exits 2 citing 16.3 (rc={}, out='{}')", r_imin.exit_code, r_imin.output));

    auto r_uni_ios = run_cfire(std::format("build \"{}\" --universal --target=ios-sim -o \"{}\" 2>&1",
        cf.string(), (tmp.path / "out_uni").string()));
    require(r_uni_ios.exit_code != 0, "--universal --target=ios-sim fails cleanly");

    auto r_ipa_host = run_cfire(std::format("build \"{}\" --ipa -o \"{}\" 2>&1",
        cf.string(), (tmp.path / "out_ipa").string()));
    require(r_ipa_host.exit_code != 0, "--ipa on non-iOS target fails cleanly");

    auto r_ipa_lin = run_cfire(std::format("build \"{}\" --ipa --target=linux -o \"{}\" 2>&1",
        cf.string(), (tmp.path / "out_ipa_lin").string()));
    require(r_ipa_lin.exit_code != 0, "--ipa --target=linux fails cleanly");

    auto r_dev_nosign = run_cfire(std::format("run \"{}\" --ios-device 2>&1", cf.string()));
    require(r_dev_nosign.exit_code != 0 &&
            (r_dev_nosign.output.contains("SignIdentity") || r_dev_nosign.output.contains("non-adhoc")),
        std::format("cfire run --ios-device without signing identity fails fast (rc={}, out='{}')",
            r_dev_nosign.exit_code, r_dev_nosign.output));

#if defined(__APPLE__)
    if (cfire::build::resolve_ios_sdk_path(true).has_value()) {
        auto cache_dir = tmp.subpath("cache");
        ScopedEnvGuard cache_guard("CFIRE_CACHE_DIR", cache_dir.string());

        auto r_bad_ent = run_cfire(std::format("build \"{}\" --target=ios-sim --entitlements /nonexistent_cfire_ent_999.plist -o \"{}\" 2>&1",
            cf.string(), (tmp.path / "out_bad_ent").string()));
        require(r_bad_ent.exit_code != 0 && r_bad_ent.output.contains("not found"),
            "invalid --entitlements path fails cleanly");

        auto bad_res_dir = tmp.subpath("missing_res_proj");
        (void)tmp.file("missing_res_proj/build.cf", R"CF(package main
import "core:build"

app := build.Executable{
    Name: "badres",
    Entry: "main.cf",
    Resources: ["does_not_exist_dir"]
}
)CF");
        auto bad_res_cf = tmp.file("missing_res_proj/main.cf", "package main\nfn main() { println(\"x\") }\n");
        auto r_bad_res = run_cfire(std::format("build \"{}\" --target=ios-sim -o \"{}\" 2>&1",
            bad_res_cf.string(), (bad_res_dir / "badres").string()));
        require(r_bad_res.exit_code != 0 && r_bad_res.output.contains("not found"),
            "missing declared build.cf resource path fails cleanly");
    }
#endif
}

inline void check_apple_bundle_packaging_and_signing([[maybe_unused]] const std::filesystem::path& rt_dir) {
#if defined(__APPLE__)
    if (!cfire::build::resolve_ios_sdk_path(true).has_value()) return;

    ScopedBuildGlobalsGuard globals_guard;
    ScopedTempDir tmp("cfire_apple_pkg_e2e");
    auto cache_dir = tmp.subpath("cache");
    ScopedEnvGuard cache_guard("CFIRE_CACHE_DIR", cache_dir.string());

    // 1. iOS Simulator ext:webui bundle + Assets.car + DT* + PrivacyInfo + minOS + ITMS-90683 + space path + .ipa
    auto spaced = tmp.subpath("App With Spaces");
    write_valid_rgba_png(spaced / "app_icon.png", 64, 64);
    (void)tmp.file("App With Spaces/assets/hello.txt", "bundle asset ok\n");
    (void)tmp.file("App With Spaces/build.cf", R"CF(package main
import "core:build"

app := build.Executable{
    Name: "SpaceApp",
    Entry: "main.cf",
    AppName: "Space & App",
    BundleId: "com.cfire.spaceapp",
    Version: "2.1.0",
    BuildNumber: "77",
    Icon: "app_icon.png",
    Resources: ["assets"],
    IosMin: "16.4",
    IosOrientations: ["UIInterfaceOrientationPortrait"],
    IosDeviceFamily: [1],
    InfoPlist: { "NSFaceIDUsageDescription": "Sign in cleanly" }
}
)CF");
    auto spaced_cf = tmp.file("App With Spaces/main.cf", R"CF(package main
import "ext:webui"
import "core:keychain"
fn main() {
    mut app := make(webui.App, "SpaceApp", 390, 844)
    defer app.close()
    _ = keychain.authenticate_biometric("Sign in") or { return }
}
)CF");

    auto ipa_out = spaced / "out" / "SpaceApp.ipa";
    auto ios_plan = cfire::build::build_native(spaced_cf, rt_dir, ipa_out, false, false, "ios-sim");
    require(ios_plan.has_value(),
        std::format("iOS Simulator SpaceApp build_native failed: {}", ios_plan ? "" : ios_plan.error()));
    if (ios_plan) {
        std::error_code ec;
        std::filesystem::path app_bundle(ios_plan->ios_app_bundle);
        auto bundle_bin = app_bundle / "SpaceApp";
        auto plist_path = app_bundle / "Info.plist";
        auto privacy_path = app_bundle / "PrivacyInfo.xcprivacy";
        require(std::filesystem::is_directory(app_bundle, ec), "SpaceApp.app directory exists");
        require(std::filesystem::exists(bundle_bin, ec), "SpaceApp Mach-O binary exists inside .app");
        require(std::filesystem::exists(app_bundle / "assets" / "hello.txt", ec),
            "declared resource directory copied into iOS .app bundle");
        require(!ios_plan->ios_ipa_path.empty() && std::filesystem::is_regular_file(ios_plan->ios_ipa_path, ec),
            ".ipa archive created from iOS .app");

        int lint_rc = 0;
        (void)cfire::bridge::run_command_capture(
            std::format("plutil -lint {} 2>&1", cfire::bridge::shell_quote(plist_path.string())), lint_rc);
        require(lint_rc == 0, "iOS Info.plist passes plutil -lint");

        auto plist_txt = cfire::build::read_file_text(plist_path);
        require(plist_txt &&
                plist_txt->contains("<key>MinimumOSVersion</key>\n    <string>16.4</string>") &&
                plist_txt->contains("<key>CFBundleShortVersionString</key>\n    <string>2.1.0</string>") &&
                plist_txt->contains("<key>CFBundleVersion</key>\n    <string>77</string>") &&
                plist_txt->contains("<key>DTXcode</key>") &&
                plist_txt->contains("<key>DTSDKName</key>") &&
                plist_txt->contains("<key>NSFaceIDUsageDescription</key>") &&
                plist_txt->contains("<key>UIApplicationSceneManifest</key>") &&
                plist_txt->contains("<string>CfireWebUISceneDelegate</string>"),
            "iOS Info.plist contains expected metadata, DT* keys, and UIApplicationSceneManifest -> CfireWebUISceneDelegate");

        int priv_rc = 0;
        (void)cfire::bridge::run_command_capture(
            std::format("plutil -lint {} 2>&1", cfire::bridge::shell_quote(privacy_path.string())), priv_rc);
        require(priv_rc == 0 && std::filesystem::exists(privacy_path, ec),
            "iOS bundle includes valid PrivacyInfo.xcprivacy");

        require(std::filesystem::exists(app_bundle / "Assets.car", ec) ||
                std::filesystem::exists(app_bundle / "AppIcon60x60@2x.png", ec),
            "iOS bundle compiles Assets.car (or fallback AppIcon PNG)");

        int otool_l_rc = 0;
        std::string otool_l_out = cfire::bridge::run_command_capture(
            std::format("otool -l {} 2>&1", cfire::bridge::shell_quote(bundle_bin.string())), otool_l_rc);
        require(otool_l_rc == 0 && otool_l_out.contains("minos 16.4"),
            std::format("otool -l reports minos 16.4 matching IosMin (got: {})", otool_l_out));

        int otool_L_rc = 0;
        std::string otool_L_out = cfire::bridge::run_command_capture(
            std::format("otool -L {} 2>&1", cfire::bridge::shell_quote(bundle_bin.string())), otool_L_rc);
        int nm_rc = 0;
        std::string nm_out = cfire::bridge::run_command_capture(
            std::format("nm -u {} 2>&1", cfire::bridge::shell_quote(bundle_bin.string())), nm_rc);
        require(otool_L_rc == 0 &&
                !otool_L_out.contains("AVFoundation.framework") &&
                !otool_L_out.contains("Photos.framework") &&
                !nm_out.contains("AVCaptureDevice") &&
                !nm_out.contains("PHPhotoLibrary"),
            "iOS ext:webui binary contains zero unlinked/unused AVFoundation/Photos references (ITMS-90683)");

        int cs_rc = 0;
        (void)cfire::bridge::run_command_capture(
            std::format("codesign -v {} 2>&1", cfire::bridge::shell_quote(app_bundle.string())), cs_rc);
        require(cs_rc == 0, "codesign -v verifies inside-out signed iOS .app bundle with spaces in path");

        int unzip_rc = 0;
        std::string unzip_out = cfire::bridge::run_command_capture(
            std::format("unzip -l {} 2>&1", cfire::bridge::shell_quote(ios_plan->ios_ipa_path)), unzip_rc);
        require(unzip_rc == 0 && unzip_out.contains("Payload/SpaceApp.app/Info.plist"),
            ".ipa archive contains Payload/SpaceApp.app/Info.plist");
    }

    // 2. macOS DevDebug GUI .app maintains minos consistency with LSMinimumSystemVersion
    {
        ScopedBuildGlobalsGuard mac_debug_guard;
        cfire::build::set_build_profile(cfire::build::BuildProfile::DevDebug);
        auto mac_cf = tmp.file("mac_debug_gui/main.cf", R"CF(package main
import "ext:webui"
fn main() {
    mut app := make(webui.App, "MacDbg", 640, 480)
    defer app.close()
}
)CF");
        auto mac_bin = tmp.path / "mac_debug_out" / "MacDbg";
        auto mac_plan = cfire::build::build_native(mac_cf, rt_dir, mac_bin);
        require(mac_plan.has_value() && !mac_plan->macos_app_bundle.empty(),
            std::format("macOS DevDebug GUI .app build succeeded: {}", mac_plan ? "" : mac_plan.error()));
        if (mac_plan && !mac_plan->macos_app_bundle.empty()) {
            std::filesystem::path mb(mac_plan->macos_app_bundle);
            auto plist_p = mb / "Contents" / "Info.plist";
            int pl_rc = 0;
            (void)cfire::bridge::run_command_capture(
                std::format("plutil -lint {} 2>&1", cfire::bridge::shell_quote(plist_p.string())), pl_rc);
            require(pl_rc == 0, "macOS DevDebug Info.plist passes plutil -lint");

            int ot_rc = 0;
            std::string ot_out = cfire::bridge::run_command_capture(
                std::format("otool -l {} 2>&1", cfire::bridge::shell_quote((mb / "Contents" / "MacOS" / "MacDbg").string())), ot_rc);
            require(ot_rc == 0 && ot_out.contains("minos 13.3"),
                "macOS DevDebug GUI .app binary minos 13.3 matches LSMinimumSystemVersion 13.3");

            int cs_rc = 0;
            (void)cfire::bridge::run_command_capture(
                std::format("codesign -v {} 2>&1", cfire::bridge::shell_quote(mb.string())), cs_rc);
            require(cs_rc == 0, "codesign -v verifies macOS .app bundle");
        }
    }

    // 3. Device ios build without SignIdentity fails cleanly
    if (cfire::build::resolve_ios_sdk_path(false).has_value()) {
        auto dev_cf = tmp.file("dev_nosign/main.cf", "package main\nfn main() { println(\"x\") }\n");
        auto dev_res = cfire::build::build_native(dev_cf, rt_dir, tmp.path / "dev_nosign_out" / "App", false, false, "ios");
        require(!dev_res.has_value() && dev_res.error().contains("SignIdentity"),
            "device ios build without SignIdentity fails cleanly requiring non-adhoc signing");
    }

    // 4. iOS Simulator Objective-C++ compile check with -Wall -Wextra -fobjc-arc (zero warnings)
    if (auto sim_sdk = cfire::build::resolve_ios_sdk_path(true); sim_sdk.has_value()) {
        auto ios_mm = tmp.file("ios_webui_sim.mm", R"MM(
#import <UIKit/UIKit.h>
#import <WebKit/WebKit.h>
#import <SafariServices/SafariServices.h>
#import <UserNotifications/UserNotifications.h>
#include "core/webui.hpp"

int ios_compile_smoke() {
    auto app = cfire::webui::new_app("iOSSimSmoke", 390, 844);
    app.allow_navigation_origin("https://auth.example.com");
    (void)app.initial_url();
    (void)app.native_window_handle();
    (void)app.native_webview_handle();
    (void)app.native_view_controller_handle();
    Class clsScene = [CfireWebUISceneDelegate class];
    Class clsVC = [CfireWebUIViewController class];
    Class clsNav = [CfireWebUINavigationDelegate class];
    Class clsNotif = [CfireWebUINotificationDelegate class];
    Class clsSafari = [SFSafariViewController class];
    NSNotificationName kbd = UIKeyboardWillShowNotification;
    UIPasteboard* pb = [UIPasteboard generalPasteboard];
    return (clsScene && clsVC && clsNav && clsNotif && clsSafari && kbd && pb) ? 0 : 1;
}
)MM");
        auto ios_obj = tmp.path / "ios_webui_sim.o";
#if defined(__aarch64__) || defined(_M_ARM64)
        const std::string ios_target = std::format("arm64-apple-ios{}-simulator", cfire::build::kDefaultIosMinVersion);
#else
        const std::string ios_target = std::format("x86_64-apple-ios{}-simulator", cfire::build::kDefaultIosMinVersion);
#endif
        std::string comp_cmd = std::format(
            "clang++ -std=c++2b -Wall -Wextra -fobjc-arc -x objective-c++ -target {} -isysroot {} -I{} -c {} -o {} 2>&1",
            ios_target,
            cfire::bridge::shell_quote(*sim_sdk),
            cfire::bridge::shell_quote(rt_dir.string()),
            cfire::bridge::shell_quote(ios_mm.string()),
            cfire::bridge::shell_quote(ios_obj.string()));
        int sim_rc = 0;
        std::string sim_out = cfire::bridge::run_command_capture(comp_cmd, sim_rc);
        require(sim_rc == 0, std::format("iOS Simulator WebUI ObjC++ compile failed (rc={}):\n{}", sim_rc, sim_out));
        require(!sim_out.contains("warning:"), std::format("iOS Simulator WebUI ObjC++ emitted warnings:\n{}", sim_out));
    }
#endif
}

inline void check_windows_versioninfo_packaging([[maybe_unused]] const std::filesystem::path& rt_dir) {
#if defined(_WIN32)
    ScopedTempDir tmp("cfire_win_ver_e2e");
    auto cache_dir = tmp.subpath("cache");
    ScopedEnvGuard cache_guard("CFIRE_CACHE_DIR", cache_dir.string());

    (void)tmp.file("build.cf", R"CF(package main
import "core:build"

app := build.Executable{
    Name: "winmeta",
    Entry: "main.cf",
    AppName: "CfireWinMeta",
    Version: "2.4.1",
    BuildNumber: "42"
}
)CF");
    auto win_cf = tmp.file("main.cf", "package main\nfn main() {\n    println(\"winmeta-ok\")\n}\n");
    auto out_exe = tmp.path / "winmeta.exe";
    auto res_obj = cfire::build::compile_windows_icon("", out_exe, "", "2.4.1", "42", "CfireWinMeta");
    if (!cfire::build::resolve_windres("").empty()) {
        std::error_code ec;
        require(!res_obj.empty() && std::filesystem::is_regular_file(res_obj, ec),
            "Windows windres compiles VERSIONINFO-only .res.o without requiring an .ico");
    }
    auto plan = cfire::build::build_native(win_cf, rt_dir, out_exe);
    require(plan.has_value(),
        std::format("Windows VERSIONINFO build_native succeeded: {}", plan ? "" : plan.error()));
    if (plan) {
        std::error_code ec;
        require(std::filesystem::exists(out_exe, ec), "Windows VERSIONINFO exe exists");
        int run_rc = 0;
        std::string run_out = cfire::bridge::run_command_capture(
            std::format("\"{}\" 2>&1", out_exe.string()), run_rc);
        require(run_rc == 0 && run_out.contains("winmeta-ok"),
            "Windows VERSIONINFO executable runs cleanly");
    }
#endif
}

inline void check_wave_f8_manifest_conditionals_and_step([[maybe_unused]] const std::filesystem::path& rt_dir) {
    ScopedTempDir tmp("cfire_f8_manifest");
    auto bcf = tmp.file("build.cf", R"CF(package build

import "core:build"

is_mac := build.os() == "macos"
is_linux := build.os() == "linux"
is_win := build.os() == "windows"
is_arm := build.arch() == "arm64"

os_tag := match build.os() {
    "macos" => "MAC_TARGET=1",
    "linux" => "LIN_TARGET=1",
    _ => "WIN_TARGET=1",
}

extra_libs := if is_linux && !is_win { ["m"] } else { [] }
plat_cflags := if is_mac && is_arm { ["-DARCH_MAC_ARM=1"] } else { ["-DARCH_OTHER=1"] }

GenStep := build.Step("gen_hdr", {
    Command: "sh",
    Args: ["-c", "echo '#define STEP_GENERATED 77' > step_gen.h"],
    Outputs: ["step_gen.h"]
})

App := build.Executable("condapp", {
    Entry: "main.cf",
    Includes: ["csrc"],
    Sources: ["csrc/helper.c"],
    Defines: [os_tag],
    Frameworks: if is_mac { ["Foundation"] } else { [] },
    Libraries: extra_libs + if is_win { ["ws2_32"] } else { [] },
    CFlags: plat_cflags,
    LDFlags: if is_linux { ["-Wl,--as-needed"] } else { [] },
})
)CF");

    // 1. Target-triple conditional evaluation across macOS/Linux/Windows
    {
        auto pm = cfire::build::parse_build_cf(bcf, "arm64-apple-macos");
        require(pm.has_value() && pm->executables.size() == 1 && pm->pre_build_commands.size() == 1,
            std::format("CF-268: parse_build_cf(arm64-apple-macos) ok: {}", pm ? "" : pm.error()));
        if (pm && !pm->executables.empty() && !pm->pre_build_commands.empty()) {
            const auto& st = pm->pre_build_commands[0];
            require(st.command == "sh" && st.outputs.size() == 1 && st.outputs[0] == "step_gen.h",
                "CF-268: build.Step parses Command/Args/Outputs identically to build.Command");
            const auto& ex = pm->executables[0];
            require(ex.defines.size() == 1 && ex.defines[0] == "MAC_TARGET=1",
                "CF-268: match build.os() evaluates arm64-apple-macos");
            require(ex.frameworks.size() == 1 && ex.frameworks[0] == "Foundation",
                "CF-268: if is_mac evaluates Frameworks on macOS");
            require(ex.libraries.empty() && ex.ldflags.empty(),
                "CF-268: false OS branches omit Linux/Windows libs");
            require(ex.cflags.size() == 1 && ex.cflags[0] == "-DARCH_MAC_ARM=1",
                "CF-268: build.os() && build.arch() evaluates CFlags");
            require(ex.sources.size() == 1 && ex.sources[0] == "csrc/helper.c",
                "CF-268: Sources evaluated on Executable");
        }

        auto pl = cfire::build::parse_build_cf(bcf, "x86_64-unknown-linux-gnu");
        require(pl.has_value() && !pl->executables.empty(),
            "CF-268: parse_build_cf(x86_64-unknown-linux-gnu) ok");
        if (pl && !pl->executables.empty()) {
            const auto& ex = pl->executables[0];
            require(ex.defines.size() == 1 && ex.defines[0] == "LIN_TARGET=1", "CF-268: LIN_TARGET=1 on Linux");
            require(ex.libraries.size() == 1 && ex.libraries[0] == "m", "CF-268: Libraries evaluates on Linux");
            require(ex.ldflags.size() == 1 && ex.ldflags[0] == "-Wl,--as-needed", "CF-268: LDFlags evaluates on Linux");
            require(ex.cflags.size() == 1 && ex.cflags[0] == "-DARCH_OTHER=1", "CF-268: non-arm64 branch CFlags");
        }

        auto pw = cfire::build::parse_build_cf(bcf, "x86_64-w64-mingw32");
        require(pw.has_value() && !pw->executables.empty(),
            "CF-268: parse_build_cf(x86_64-w64-mingw32) ok");
        if (pw && !pw->executables.empty()) {
            const auto& ex = pw->executables[0];
            require(ex.defines.size() == 1 && ex.defines[0] == "WIN_TARGET=1", "CF-268: WIN_TARGET=1 on Windows");
            require(ex.libraries.size() == 1 && ex.libraries[0] == "ws2_32", "CF-268: Windows branch Libraries");
        }
    }

    // 2. Non-constant expression in build.cf fails with cfire/INVALID-BUILD-VALUE
    {
        auto bad_bcf = tmp.file("bad/build.cf", R"CF(package build
import "core:build"
App := build.Executable("bad", {
    Entry: "main.cf",
    Libraries: [unknown_helper()],
})
)CF");
        auto bad_res = cfire::build::parse_build_cf(bad_bcf);
        require(!bad_res.has_value() && bad_res.error().contains("cfire/INVALID-BUILD-VALUE"),
            std::format("CF-268: non-constant build.cf fails with INVALID-BUILD-VALUE, got: {}",
                        bad_res ? "<ok>" : bad_res.error()));
    }

#if !defined(_WIN32)
    // 3. End-to-end build with build.Step + Sources + CFlags + conditional Defines (host POSIX sh)
    {
        (void)tmp.file("csrc/helper.c", "#include \"../step_gen.h\"\nint helper_val(void) { return STEP_GENERATED + ARCH_NUM; }\n");
        (void)tmp.file("csrc/helper.h", "int helper_val(void);\n");
        auto bcf_e2e = tmp.file("e2e/build.cf", R"CF(package build
import "core:build"
Gen := build.Step("gen_hdr", {
    Command: "/bin/sh",
    Args: ["-c", "echo '#define STEP_GENERATED 77' > step_gen.h"],
    Outputs: ["step_gen.h"]
})
App := build.Executable("cond_e2e", {
    Entry: "main.cf",
    Includes: ["csrc"],
    Sources: ["csrc/helper.c"],
    Defines: {"ARCH_NUM": if build.os() == "macos" || build.os() == "linux" { 10 } else { 0 }},
    CFlags: ["-DCF_COND_CFLAG=3"],
})
)CF");
        (void)tmp.file("e2e/csrc/helper.h", "#ifdef __cplusplus\nextern \"C\" {\n#endif\nint helper_val(void);\n#ifdef __cplusplus\n}\n#endif\n");
        (void)tmp.file("e2e/csrc/helper.c", "#include \"../step_gen.h\"\nint helper_val(void) { return STEP_GENERATED + ARCH_NUM + CF_COND_CFLAG; }\n");
        auto main_cf = tmp.file("e2e/main.cf", R"CF(package main
import cpp "helper.h"
fn main() {
    println("step={}", cpp.helper_val())
}
)CF");
        auto cache_dir = tmp.subpath("e2e_cache");
        ScopedEnvGuard cache_guard("CFIRE_CACHE_DIR", cache_dir.string());
        auto out_bin = tmp.path / "e2e_bin";
        auto plan = cfire::build::build_native(main_cf, rt_dir, out_bin);
        require(plan.has_value(),
            std::format("CF-268: build_native with build.Step + Sources + CFlags ok: {}", plan ? "" : plan.error()));
        if (plan) {
            require(std::filesystem::exists(tmp.path / "e2e" / "step_gen.h"),
                "CF-268: build.Step materialized step_gen.h");
            int rc = 0;
            std::string run_out = cfire::bridge::run_command_capture(
                cfire::bridge::shell_quote(out_bin.string()) + " 2>&1", rc);
            require(rc == 0 && run_out.contains("step=90"),
                std::format("CF-268: build.cf Sources + CFlags linked cleanly, got rc={} out={}", rc, run_out));
        }
    }
#endif
}

} // namespace test_build_manifest_ios_detail

void test_build_manifest_ios(const std::filesystem::path& rt_dir) {
    std::println("=== RUN   test_build_manifest_ios");
    test_build_manifest_ios_detail::check_os_dirs_and_open_url();
    test_build_manifest_ios_detail::check_keychain_biometric();
    test_build_manifest_ios_detail::check_build_and_link_smoke(rt_dir);
    test_build_manifest_ios_detail::check_packaging_manifest_and_helpers(rt_dir);
    test_build_manifest_ios_detail::check_cli_packaging_flags();
    test_build_manifest_ios_detail::check_apple_bundle_packaging_and_signing(rt_dir);
    test_build_manifest_ios_detail::check_windows_versioninfo_packaging(rt_dir);
    test_build_manifest_ios_detail::check_wave_f8_manifest_conditionals_and_step(rt_dir);
    std::println("--- PASS: test_build_manifest_ios");
}
