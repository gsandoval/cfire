#pragma once
// src/build/apple_bundle.hpp — macOS `.app` & iOS `.app` Bundle Packaging, Icon Generation & Simulator Runner

#include "manifest.hpp"

namespace cfire::build {

[[nodiscard]] inline auto ensure_macos_icns(
    const std::filesystem::path& icon_src,
    [[maybe_unused]] const std::filesystem::path& cache_dir
) -> std::expected<std::filesystem::path, std::string> {
    std::error_code ec;
    if (!std::filesystem::exists(icon_src, ec)) {
        return std::unexpected(std::format("icon file not found: {}", icon_src.string()));
    }
    if (icon_src.extension() == ".icns") {
        return icon_src;
    }
    if (icon_src.extension() != ".png" && icon_src.extension() != ".svg") {
        return std::unexpected(std::format("unsupported icon format '{}'; expected .png, .svg, or .icns", icon_src.extension().string()));
    }

#if defined(__APPLE__)
    std::filesystem::create_directories(cache_dir, ec);
    auto stem = icon_src.stem().string();
    auto cached_icns = cache_dir / (stem + ".icns");

    if (std::filesystem::exists(cached_icns, ec)) {
        auto src_time = std::filesystem::last_write_time(icon_src, ec);
        auto icns_time = std::filesystem::last_write_time(cached_icns, ec);
        if (!ec && icns_time >= src_time) {
            return cached_icns;
        }
    }

    std::filesystem::path raster_src = icon_src;
    if (icon_src.extension() == ".svg") {
        int ql_rc = 0;
        std::string ql_cmd = std::format("/usr/bin/qlmanage -t -s 1024 -o {} {} >/dev/null 2>&1",
            bridge::shell_quote(cache_dir.string()),
            bridge::shell_quote(icon_src.string()));
        (void)bridge::run_command_capture(ql_cmd, ql_rc);
        auto ql_png = cache_dir / (icon_src.filename().string() + ".png");
        if (std::filesystem::exists(ql_png, ec)) {
            raster_src = ql_png;
        } else {
            auto rsvg_png = cache_dir / (stem + "_1024.png");
            int rs_rc = 0;
            std::string rs_cmd = std::format("rsvg-convert -w 1024 -h 1024 {} -o {} >/dev/null 2>&1",
                bridge::shell_quote(icon_src.string()),
                bridge::shell_quote(rsvg_png.string()));
            (void)bridge::run_command_capture(rs_cmd, rs_rc);
            if (std::filesystem::exists(rsvg_png, ec)) {
                raster_src = rsvg_png;
            }
        }
    }

    auto iconset_dir = cache_dir / (stem + ".iconset");
    std::filesystem::create_directories(iconset_dir, ec);

    struct ScaleEntry { int size; const char* filename; };
    const ScaleEntry scales[] = {
        {16, "icon_16x16.png"},
        {32, "icon_16x16@2x.png"},
        {32, "icon_32x32.png"},
        {64, "icon_32x32@2x.png"},
        {128, "icon_128x128.png"},
        {256, "icon_128x128@2x.png"},
        {256, "icon_256x256.png"},
        {512, "icon_256x256@2x.png"},
        {512, "icon_512x512.png"},
        {1024, "icon_512x512@2x.png"},
    };

    std::string q_src = bridge::shell_quote(raster_src.string());
    for (const auto& sc : scales) {
        auto out_p = iconset_dir / sc.filename;
        std::string q_out = bridge::shell_quote(out_p.string());
        int rc = 0;
        std::string sips_cmd = std::format("/usr/bin/sips -z {} {} {} --out {} >/dev/null 2>&1", sc.size, sc.size, q_src, q_out);
        (void)bridge::run_command_capture(sips_cmd, rc);
    }

    int iu_rc = 0;
    std::string iu_cmd = std::format("/usr/bin/iconutil -c icns {} -o {} >/dev/null 2>&1",
        bridge::shell_quote(iconset_dir.string()),
        bridge::shell_quote(cached_icns.string()));
    (void)bridge::run_command_capture(iu_cmd, iu_rc);

    std::filesystem::remove_all(iconset_dir, ec);

    if (iu_rc == 0 && std::filesystem::exists(cached_icns, ec)) {
        return cached_icns;
    }
    return std::unexpected(std::format("failed to generate .icns from '{}' using iconutil", icon_src.string()));
#else
    return std::unexpected("automatic .png to .icns conversion is only supported on macOS");
#endif
}


inline void package_apple_app_bundles(
    const std::filesystem::path& cf_file,
    const std::filesystem::path& out_bin,
    const BuildPlan& plan
) {
    if (!plan.macos_app_bundle.empty()) {
        std::error_code ec;
        auto app_bundle = std::filesystem::path(plan.macos_app_bundle);
        auto macos_dir = app_bundle / "Contents" / "MacOS";
        std::filesystem::create_directories(macos_dir, ec);
        std::string bin_name = out_bin.extension() == ".app" ? out_bin.stem().string() : out_bin.filename().string();
        auto bundle_bin = macos_dir / bin_name;
        std::filesystem::copy_file(out_bin, bundle_bin, std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::permissions(bundle_bin,
            std::filesystem::perms::owner_all | std::filesystem::perms::group_read | std::filesystem::perms::group_exec |
            std::filesystem::perms::others_read | std::filesystem::perms::others_exec, ec);

        std::string effective_name = plan.app_name.empty() ? bin_name : plan.app_name;
        std::string effective_bundle_id = plan.bundle_id.empty() ? ("com.cfire." + bin_name) : plan.bundle_id;
        bool has_icon = false;

        if (!plan.icon_path.empty()) {
            std::filesystem::path icon_p = plan.icon_path;
            if (icon_p.is_relative() && !std::filesystem::exists(icon_p)) {
                if (std::filesystem::exists(cf_file.parent_path() / icon_p)) {
                    icon_p = cf_file.parent_path() / icon_p;
                }
            }
            auto resources_dir = app_bundle / "Contents" / "Resources";
            std::filesystem::create_directories(resources_dir, ec);
            auto icns_res = ensure_macos_icns(icon_p, app_bundle.parent_path() / ".cfire" / "cache" / "icons");
            if (icns_res) {
                auto dest_icns = resources_dir / "AppIcon.icns";
                std::filesystem::copy_file(*icns_res, dest_icns, std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec) {
                    has_icon = true;
                }
            }
        }

        auto plist_path = app_bundle / "Contents" / "Info.plist";
        std::ofstream plist(plist_path);
        plist << std::format(R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>{}</string>
    <key>CFBundleIdentifier</key>
    <string>{}</string>
    <key>CFBundleName</key>
    <string>{}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>LSMinimumSystemVersion</key>
    <string>12.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
)", bin_name, effective_bundle_id, effective_name);

        if (has_icon) {
            plist << "    <key>CFBundleIconFile</key>\n    <string>AppIcon</string>\n";
        }
        if (!plan.url_schemes.empty()) {
            plist << "    <key>CFBundleURLTypes</key>\n    <array>\n        <dict>\n";
            plist << std::format("            <key>CFBundleURLName</key>\n            <string>{}</string>\n", effective_bundle_id);
            plist << "            <key>CFBundleURLSchemes</key>\n            <array>\n";
            for (const auto& sch : plan.url_schemes) {
                plist << std::format("                <string>{}</string>\n", sch);
            }
            plist << "            </array>\n        </dict>\n    </array>\n";
        }
        plist << "</dict>\n</plist>\n";
        plist.close();

        auto ui_src_dir = cf_file.parent_path() / "ui";
        if (std::filesystem::exists(ui_src_dir, ec) && std::filesystem::is_directory(ui_src_dir, ec)) {
            auto ui_dest_dir = app_bundle / "Contents" / "Resources" / "ui";
            std::filesystem::create_directories(ui_dest_dir, ec);
            std::filesystem::copy(ui_src_dir, ui_dest_dir,
                std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
        }

#if defined(__APPLE__)
        int cs_rc = 0;
        std::string sign_id = plan.sign_identity.empty() ? "-" : plan.sign_identity;
        std::string cs_cmd = std::format("codesign --force --deep --sign {} \"{}\" 2>/dev/null",
            bridge::shell_quote(sign_id), app_bundle.string());
        (void)bridge::run_command_capture(cs_cmd, cs_rc);
#endif
    }

    if (!plan.ios_app_bundle.empty()) {
        std::error_code ec;
        auto app_bundle = std::filesystem::path(plan.ios_app_bundle);
        std::filesystem::create_directories(app_bundle, ec);
        std::string bin_name = out_bin.extension() == ".app" ? out_bin.stem().string() : out_bin.filename().string();
        auto bundle_bin = app_bundle / bin_name;
        if (out_bin != bundle_bin) {
            std::filesystem::copy_file(out_bin, bundle_bin, std::filesystem::copy_options::overwrite_existing, ec);
        }
        std::filesystem::permissions(bundle_bin,
            std::filesystem::perms::owner_all | std::filesystem::perms::group_read | std::filesystem::perms::group_exec |
            std::filesystem::perms::others_read | std::filesystem::perms::others_exec, ec);

        auto ui_src_dir = cf_file.parent_path() / "ui";
        if (std::filesystem::exists(ui_src_dir, ec) && std::filesystem::is_directory(ui_src_dir, ec)) {
            auto ui_dest_dir = app_bundle / "ui";
            std::filesystem::create_directories(ui_dest_dir, ec);
            std::filesystem::copy(ui_src_dir, ui_dest_dir,
                std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
        }

        std::string effective_name = plan.app_name.empty() ? bin_name : plan.app_name;
        std::string effective_bundle_id = plan.bundle_id.empty() ? ("com.cfire." + bin_name) : plan.bundle_id;
        std::string platform_str = plan.is_ios_sim ? "iPhoneSimulator" : "iPhoneOS";
        bool prefer_landscape = plan.window_init_width > plan.window_init_height && plan.window_init_height > 0;

        bool has_icon = false;
        if (!plan.icon_path.empty()) {
            std::filesystem::path icon_p = plan.icon_path;
            if (icon_p.is_relative() && !std::filesystem::exists(icon_p)) {
                if (std::filesystem::exists(cf_file.parent_path() / icon_p)) {
                    icon_p = cf_file.parent_path() / icon_p;
                }
            }
            if (std::filesystem::exists(icon_p, ec)) {
                auto dest_icon = app_bundle / "AppIcon60x60@2x.png";
                int sips_rc = 0;
                std::string sips_cmd = std::format("/usr/bin/sips -z 120 120 {} --out {} >/dev/null 2>&1",
                    bridge::shell_quote(icon_p.string()),
                    bridge::shell_quote(dest_icon.string()));
                (void)bridge::run_command_capture(sips_cmd, sips_rc);
                if (sips_rc == 0 && std::filesystem::exists(dest_icon, ec)) {
                    has_icon = true;
                }
            }
        }

        std::string orientations = prefer_landscape
            ? "        <string>UIInterfaceOrientationLandscapeLeft</string>\n"
              "        <string>UIInterfaceOrientationLandscapeRight</string>\n"
              "        <string>UIInterfaceOrientationPortrait</string>\n"
            : "        <string>UIInterfaceOrientationPortrait</string>\n"
              "        <string>UIInterfaceOrientationLandscapeLeft</string>\n"
              "        <string>UIInterfaceOrientationLandscapeRight</string>\n";

        auto plist_path = app_bundle / "Info.plist";
        std::ofstream plist(plist_path);
        plist << std::format(R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>{}</string>
    <key>CFBundleIdentifier</key>
    <string>{}</string>
    <key>CFBundleName</key>
    <string>{}</string>
    <key>CFBundleDisplayName</key>
    <string>{}</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>LSRequiresIPhoneOS</key>
    <true/>
    <key>CFBundleSupportedPlatforms</key>
    <array>
        <string>{}</string>
    </array>
    <key>MinimumOSVersion</key>
    <string>17.0</string>
    <key>UIDeviceFamily</key>
    <array>
        <integer>1</integer>
        <integer>2</integer>
    </array>
    <key>UILaunchScreen</key>
    <dict/>
    <key>UISupportedInterfaceOrientations</key>
    <array>
{}    </array>
)", bin_name, effective_bundle_id, effective_name, effective_name, platform_str, orientations);

        if (has_icon) {
            plist << "    <key>CFBundleIconFiles</key>\n    <array>\n        <string>AppIcon60x60</string>\n    </array>\n";
        }
        plist << "</dict>\n</plist>\n";
        plist.close();

#if defined(__APPLE__)
        int cs_rc = 0;
        std::string cs_cmd = std::format("codesign --force --sign - \"{}\" 2>/dev/null", app_bundle.string());
        (void)bridge::run_command_capture(cs_cmd, cs_rc);
#endif
    }

}

[[nodiscard]] inline auto launch_ios_simulator(
    const std::filesystem::path& app_bundle,
    const std::string& bundle_id,
    const std::string& preferred_device = "",
    const std::vector<std::string>& run_args = {}
) -> std::expected<int, std::string> {
#if defined(__APPLE__)
    if (!std::filesystem::exists(app_bundle)) {
        return std::unexpected(std::format("iOS app bundle not found: {}", app_bundle.string()));
    }
    int rc = 0;
    std::string booted_list = bridge::run_command_capture("xcrun simctl list devices booted 2>/dev/null", rc);
    bool has_booted = (booted_list.find("(Booted)") != std::string::npos);

    if (!has_booted || !preferred_device.empty()) {
        std::string avail = bridge::run_command_capture("xcrun simctl list devices available 2>/dev/null", rc);
        std::istringstream iss(avail);
        std::string line;
        std::string chosen_udid;
        std::string chosen_name;
        while (std::getline(iss, line)) {
            if (!preferred_device.empty()) {
                if (line.find(preferred_device) == std::string::npos) continue;
            } else {
                if (line.find("iPhone") == std::string::npos && line.find("iPad") == std::string::npos) continue;
            }
            auto lp = line.find('(');
            auto rp = line.find(')', lp != std::string::npos ? lp : 0);
            if (lp != std::string::npos && rp != std::string::npos && (rp - lp - 1) >= 32) {
                chosen_udid = line.substr(lp + 1, rp - lp - 1);
                chosen_name = trim_whitespace(line.substr(0, lp));
                break;
            }
        }
        if (!chosen_udid.empty()) {
            std::println("==> Booting iOS Simulator: {} ({})...", chosen_name, chosen_udid);
            (void)bridge::run_command_capture(std::format("xcrun simctl boot \"{}\" 2>/dev/null", chosen_udid), rc);
        } else if (!has_booted) {
            return std::unexpected("no available iOS Simulator devices found via `xcrun simctl list devices available`");
        }
    }

    // Bring Simulator.app (or Xcode 27+ DeviceHub.app) to front
    (void)bridge::run_command_capture(
        "open -a Simulator 2>/dev/null || open -a DeviceHub 2>/dev/null || open \"/Applications/Xcode.app/Contents/Applications/DeviceHub.app\" 2>/dev/null",
        rc);

    // Wait until booted device finishes boot sequence
    (void)bridge::run_command_capture("xcrun simctl bootstatus booted -b 2>/dev/null", rc);

    std::string inst_out = bridge::run_command_capture(
        std::format("xcrun simctl install booted {} 2>&1", bridge::shell_quote(app_bundle.string())), rc);
    if (rc != 0) {
        return std::unexpected(std::format("failed to install '{}' on iOS Simulator:\n{}", app_bundle.string(), inst_out));
    }

    // Terminate any previous running instance of the same bundle_id cleanly
    (void)bridge::run_command_capture(
        std::format("xcrun simctl terminate booted {} 2>/dev/null", bridge::shell_quote(bundle_id)), rc);

    std::println("==> Launching {} ({}) on iOS Simulator...", app_bundle.filename().string(), bundle_id);
    std::string args_suffix;
    for (const auto& a : run_args) {
        args_suffix += " " + bridge::shell_quote(a);
    }
    int status = 1;
    if (::isatty(STDIN_FILENO)) {
        std::string pty_cmd = std::format("xcrun simctl launch --console-pty booted {}{} 2>/dev/null",
            bridge::shell_quote(bundle_id), args_suffix);
        int launch_rc = std::system(pty_cmd.c_str());
        status = WIFEXITED(launch_rc) ? WEXITSTATUS(launch_rc) : launch_rc;
    }
    if (status != 0) {
        std::string plain_cmd = std::format("xcrun simctl launch booted {}{}",
            bridge::shell_quote(bundle_id), args_suffix);
        int launch_rc = std::system(plain_cmd.c_str());
        status = WIFEXITED(launch_rc) ? WEXITSTATUS(launch_rc) : launch_rc;
    }
    return status;
#else
    (void)app_bundle; (void)bundle_id; (void)preferred_device; (void)run_args;
    return std::unexpected("running iOS Simulator requires macOS with Xcode installed.");
#endif
}


} // namespace cfire::build
