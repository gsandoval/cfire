#pragma once
// src/build/apple_bundle.hpp — macOS `.app` & iOS `.app`/`.ipa` Bundle Packaging, Assets.car, Signing & Device/Sim Runners

#include "manifest.hpp"
#include "pch.hpp"

#include <algorithm>
#include <array>
#include <mutex>
#if __has_include(<print>)
#include <print>
#endif
#include <sstream>

namespace cfire::build {

[[nodiscard]] inline auto trim_bundle_ws(std::string_view sv) -> std::string {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
        sv.remove_suffix(1);
    }
    return std::string(sv);
}

[[nodiscard]] inline auto xml_escape_plist(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

[[nodiscard]] inline auto format_dt_xcode_version(std::string_view ver) -> std::string {
    if (ver.empty()) return "";
    std::array<int, 3> parts{0, 0, 0};
    int idx = 0;
    std::size_t pos = 0;
    bool any_digit = false;
    while (pos < ver.size() && idx < 3) {
        while (pos < ver.size() && (ver[pos] < '0' || ver[pos] > '9')) {
            if (ver[pos] != '.') return "";
            ++pos;
        }
        if (pos >= ver.size()) break;
        int val = 0;
        while (pos < ver.size() && ver[pos] >= '0' && ver[pos] <= '9') {
            any_digit = true;
            val = val * 10 + (ver[pos] - '0');
            ++pos;
        }
        parts[static_cast<std::size_t>(idx++)] = val;
        if (pos < ver.size() && ver[pos] == '.') ++pos;
        else break;
    }
    if (!any_digit) return "";
    return std::format("{:02d}{}{}", parts[0] % 100, parts[1] % 10, parts[2] % 10);
}

struct AppleDtMetadata {
    std::string platform_name{};
    std::string platform_version{};
    std::string platform_build{};
    std::string sdk_name{};
    std::string sdk_build{};
    std::string xcode_version{};
    std::string xcode_build{};
};

[[nodiscard]] inline auto query_apple_dt_metadata(bool is_sim) -> AppleDtMetadata {
    static std::mutex mu;
    static std::array<bool, 2> cached_ready{false, false};
    static std::array<AppleDtMetadata, 2> cached_meta{};
    const std::size_t slot = is_sim ? 1 : 0;

    std::lock_guard<std::mutex> lk(mu);
    if (cached_ready[slot]) {
        return cached_meta[slot];
    }

    AppleDtMetadata dt;
    dt.platform_name = is_sim ? "iphonesimulator" : "iphoneos";
#if defined(__APPLE__)
    auto run_trim = [](std::string_view cmd) -> std::string {
        int rc = 0;
        std::string out = bridge::run_command_capture(std::string(cmd), rc);
        return (rc == 0) ? trim_bundle_ws(out) : std::string();
    };
    dt.platform_version = run_trim(std::format("xcrun --sdk {} --show-sdk-version 2>/dev/null", dt.platform_name));
    dt.platform_build = run_trim(std::format("xcrun --sdk {} --show-sdk-build-version 2>/dev/null", dt.platform_name));
    dt.sdk_build = dt.platform_build;
    dt.sdk_name = dt.platform_version.empty() ? dt.platform_name : (dt.platform_name + dt.platform_version);

    std::string xb = run_trim("xcodebuild -version 2>/dev/null");
    if (!xb.empty()) {
        std::istringstream iss(xb);
        std::string ln;
        while (std::getline(iss, ln)) {
            std::string trimmed = trim_bundle_ws(ln);
            if (trimmed.starts_with("Xcode ")) {
                dt.xcode_version = format_dt_xcode_version(trim_bundle_ws(trimmed.substr(6)));
            } else if (trimmed.starts_with("Build version ")) {
                dt.xcode_build = trim_bundle_ws(trimmed.substr(14));
            }
        }
    }
#endif
    cached_meta[slot] = dt;
    cached_ready[slot] = true;
    return dt;
}

[[nodiscard]] inline auto generate_default_privacy_manifest_xml() -> std::string {
    return R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>NSPrivacyTracking</key>
    <false/>
    <key>NSPrivacyTrackingDomains</key>
    <array/>
    <key>NSPrivacyCollectedDataTypes</key>
    <array/>
    <key>NSPrivacyAccessedAPITypes</key>
    <array>
        <dict>
            <key>NSPrivacyAccessedAPIType</key>
            <string>NSPrivacyAccessedAPICategoryFileTimestamp</string>
            <key>NSPrivacyAccessedAPITypeReasons</key>
            <array>
                <string>C617.1</string>
            </array>
        </dict>
        <dict>
            <key>NSPrivacyAccessedAPIType</key>
            <string>NSPrivacyAccessedAPICategorySystemBootTime</string>
            <key>NSPrivacyAccessedAPITypeReasons</key>
            <array>
                <string>35F9.1</string>
            </array>
        </dict>
    </array>
</dict>
</plist>
)";
}

namespace bundle_detail {

[[nodiscard]] inline auto write_file_text(
    const std::filesystem::path& path,
    std::string_view content
) -> std::expected<void, std::string> {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        return std::unexpected(std::format("failed opening '{}' for writing", path.string()));
    }
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    ofs.flush();
    if (!ofs.good()) {
        return std::unexpected(std::format("failed writing '{}'", path.string()));
    }
    return {};
}

inline void emit_url_types_plist(
    std::ostringstream& plist,
    const BuildPlan& plan,
    std::string_view effective_bundle_id
) {
    if (plan.url_schemes.empty()) return;
    plist << "    <key>CFBundleURLTypes</key>\n    <array>\n        <dict>\n";
    plist << std::format("            <key>CFBundleURLName</key>\n            <string>{}</string>\n",
        xml_escape_plist(effective_bundle_id));
    plist << "            <key>CFBundleURLSchemes</key>\n            <array>\n";
    for (const auto& sch : plan.url_schemes) {
        plist << std::format("                <string>{}</string>\n", xml_escape_plist(sch));
    }
    plist << "            </array>\n        </dict>\n    </array>\n";
}

[[nodiscard]] inline auto plan_has_plist_extra(const BuildPlan& plan, std::string_view key) -> bool {
    for (const auto& [k, _] : plan.info_plist_extras) {
        if (k == key) return true;
    }
    return false;
}

inline void emit_plist_extras(std::ostringstream& plist, const BuildPlan& plan) {
    for (const auto& [k, v] : plan.info_plist_extras) {
        plist << std::format("    <key>{}</key>\n", xml_escape_plist(k));
        if (plan.info_plist_bool_keys.contains(k)) {
            plist << std::format("    <{}/>\n", (v == "true") ? "true" : "false");
        } else {
            plist << std::format("    <string>{}</string>\n", xml_escape_plist(v));
        }
    }
}

[[nodiscard]] inline auto resolve_relative_project_path(
    const std::string& raw_path,
    const std::filesystem::path& project_root
) -> std::filesystem::path {
    std::filesystem::path p(raw_path);
    std::error_code ec;
    if (p.is_relative() && !std::filesystem::exists(p, ec) && !project_root.empty()) {
        auto rel_cand = project_root / p;
        if (std::filesystem::exists(rel_cand, ec)) {
            return rel_cand;
        }
    }
    return p;
}

[[nodiscard]] inline auto rasterize_svg_icon_to_png(
    const std::filesystem::path& icon_src,
    const std::filesystem::path& work_dir
) -> std::filesystem::path {
    std::filesystem::path raster_src = icon_src;
    if (icon_src.extension() != ".svg") {
        return raster_src;
    }
#if defined(__APPLE__)
    std::error_code ec;
    int ql_rc = 0;
    std::string ql_cmd = std::format("/usr/bin/qlmanage -t -s 1024 -o {} {} >/dev/null 2>&1",
        bridge::shell_quote(work_dir.string()),
        bridge::shell_quote(icon_src.string()));
    (void)bridge::run_command_capture(ql_cmd, ql_rc);
    auto ql_png = work_dir / (icon_src.filename().string() + ".png");
    if (std::filesystem::exists(ql_png, ec)) {
        return ql_png;
    }
    auto rsvg_png = work_dir / (icon_src.stem().string() + "_1024.png");
    int rs_rc = 0;
    std::string rs_cmd = std::format("rsvg-convert -w 1024 -h 1024 {} -o {} >/dev/null 2>&1",
        bridge::shell_quote(icon_src.string()),
        bridge::shell_quote(rsvg_png.string()));
    (void)bridge::run_command_capture(rs_cmd, rs_rc);
    if (std::filesystem::exists(rsvg_png, ec)) {
        return rsvg_png;
    }
#else
    (void)work_dir;
#endif
    return raster_src;
}

[[nodiscard]] inline auto extract_partial_plist_inner_xml(std::string_view plist_xml) -> std::string {
    auto dict_open = plist_xml.find("<dict>");
    if (dict_open == std::string_view::npos) return "";
    auto content_start = dict_open + 6;
    while (content_start < plist_xml.size() &&
           (plist_xml[content_start] == '\r' || plist_xml[content_start] == '\n')) {
        ++content_start;
    }
    auto dict_close = plist_xml.rfind("</dict>");
    if (dict_close == std::string_view::npos || dict_close <= content_start) return "";
    std::string inner(plist_xml.substr(content_start, dict_close - content_start));
    if (!inner.empty() && inner.back() != '\n') inner.push_back('\n');
    return inner;
}

} // namespace bundle_detail

[[nodiscard]] inline auto generate_macos_info_plist(
    const BuildPlan& plan,
    const std::string& bin_name,
    bool has_icon
) -> std::string {
    std::string effective_name = plan.app_name.empty() ? bin_name : plan.app_name;
    std::string effective_bundle_id = plan.bundle_id.empty() ? ("com.cfire." + bin_name) : plan.bundle_id;
    std::string effective_ver = plan.version.empty() ? "1.0" : plan.version;
    std::string effective_build = plan.build_number.empty() ? "1" : plan.build_number;
    std::string effective_min = plan.macos_min.empty() ? std::string(kDefaultMacosMinVersion) : plan.macos_min;

    std::ostringstream plist;
    plist << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
          << "<plist version=\"1.0\">\n<dict>\n"
          << std::format("    <key>CFBundleExecutable</key>\n    <string>{}</string>\n", xml_escape_plist(bin_name))
          << std::format("    <key>CFBundleIdentifier</key>\n    <string>{}</string>\n", xml_escape_plist(effective_bundle_id))
          << std::format("    <key>CFBundleName</key>\n    <string>{}</string>\n", xml_escape_plist(effective_name))
          << "    <key>CFBundlePackageType</key>\n    <string>APPL</string>\n"
          << std::format("    <key>CFBundleShortVersionString</key>\n    <string>{}</string>\n", xml_escape_plist(effective_ver))
          << std::format("    <key>CFBundleVersion</key>\n    <string>{}</string>\n", xml_escape_plist(effective_build))
          << std::format("    <key>LSMinimumSystemVersion</key>\n    <string>{}</string>\n", xml_escape_plist(effective_min));
    if (!bundle_detail::plan_has_plist_extra(plan, "NSHighResolutionCapable")) {
        plist << "    <key>NSHighResolutionCapable</key>\n    <true/>\n";
    }

    if (has_icon && !bundle_detail::plan_has_plist_extra(plan, "CFBundleIconFile")) {
        plist << "    <key>CFBundleIconFile</key>\n    <string>AppIcon</string>\n";
    }
    bundle_detail::emit_url_types_plist(plist, plan, effective_bundle_id);
    bundle_detail::emit_plist_extras(plist, plan);
    plist << "</dict>\n</plist>\n";
    return plist.str();
}

[[nodiscard]] inline auto generate_ios_info_plist(
    const BuildPlan& plan,
    const std::string& bin_name,
    const AppleDtMetadata& dt,
    const std::string& partial_icon_xml,
    bool has_fallback_icon
) -> std::string {
    std::string effective_name = plan.app_name.empty() ? bin_name : plan.app_name;
    std::string effective_bundle_id = plan.bundle_id.empty() ? ("com.cfire." + bin_name) : plan.bundle_id;
    std::string effective_ver = plan.version.empty() ? "1.0" : plan.version;
    std::string effective_build = plan.build_number.empty() ? "1" : plan.build_number;
    std::string effective_ios_min = plan.ios_min.empty() ? std::string(kDefaultIosMinVersion) : plan.ios_min;
    std::string platform_str = plan.is_ios_sim ? "iPhoneSimulator" : "iPhoneOS";

    std::vector<std::string> orientations = plan.ios_orientations;
    if (orientations.empty()) {
        orientations = {
            "UIInterfaceOrientationPortrait",
            "UIInterfaceOrientationLandscapeLeft",
            "UIInterfaceOrientationLandscapeRight",
        };
    }
    std::vector<int> device_family = plan.ios_device_family;
    if (device_family.empty()) {
        device_family = {1, 2};
    }

    std::ostringstream plist;
    plist << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
          << "<plist version=\"1.0\">\n<dict>\n"
          << std::format("    <key>CFBundleExecutable</key>\n    <string>{}</string>\n", xml_escape_plist(bin_name))
          << std::format("    <key>CFBundleIdentifier</key>\n    <string>{}</string>\n", xml_escape_plist(effective_bundle_id))
          << std::format("    <key>CFBundleName</key>\n    <string>{}</string>\n", xml_escape_plist(effective_name));
    if (!bundle_detail::plan_has_plist_extra(plan, "CFBundleDisplayName")) {
        plist << std::format("    <key>CFBundleDisplayName</key>\n    <string>{}</string>\n", xml_escape_plist(effective_name));
    }
    plist << "    <key>CFBundleInfoDictionaryVersion</key>\n    <string>6.0</string>\n"
          << "    <key>CFBundlePackageType</key>\n    <string>APPL</string>\n"
          << std::format("    <key>CFBundleShortVersionString</key>\n    <string>{}</string>\n", xml_escape_plist(effective_ver))
          << std::format("    <key>CFBundleVersion</key>\n    <string>{}</string>\n", xml_escape_plist(effective_build))
          << "    <key>LSRequiresIPhoneOS</key>\n    <true/>\n"
          << "    <key>CFBundleSupportedPlatforms</key>\n    <array>\n"
          << std::format("        <string>{}</string>\n", platform_str)
          << "    </array>\n"
          << std::format("    <key>MinimumOSVersion</key>\n    <string>{}</string>\n", xml_escape_plist(effective_ios_min))
          << "    <key>UIDeviceFamily</key>\n    <array>\n";
    for (int fam : device_family) {
        plist << std::format("        <integer>{}</integer>\n", fam);
    }
    plist << "    </array>\n";
    if (!bundle_detail::plan_has_plist_extra(plan, "UILaunchScreen")) {
        plist << "    <key>UILaunchScreen</key>\n    <dict/>\n";
    }
    if (!bundle_detail::plan_has_plist_extra(plan, "UIApplicationSceneManifest")) {
        plist << "    <key>UIApplicationSceneManifest</key>\n"
              << "    <dict>\n"
              << "        <key>UIApplicationSupportsMultipleScenes</key>\n"
              << "        <false/>\n"
              << "        <key>UISceneConfigurations</key>\n"
              << "        <dict>\n"
              << "            <key>UIWindowSceneSessionRoleApplication</key>\n"
              << "            <array>\n"
              << "                <dict>\n"
              << "                    <key>UISceneConfigurationName</key>\n"
              << "                    <string>Default Configuration</string>\n"
              << "                    <key>UISceneDelegateClassName</key>\n"
              << "                    <string>CfireWebUISceneDelegate</string>\n"
              << "                </dict>\n"
              << "            </array>\n"
              << "        </dict>\n"
              << "    </dict>\n";
    }
    plist << "    <key>UISupportedInterfaceOrientations</key>\n    <array>\n";
    for (const auto& ori : orientations) {
        plist << std::format("        <string>{}</string>\n", xml_escape_plist(ori));
    }
    plist << "    </array>\n";

    auto emit_dt = [&](std::string_view k, std::string_view v) {
        if (!v.empty() && !bundle_detail::plan_has_plist_extra(plan, k)) {
            plist << std::format("    <key>{}</key>\n    <string>{}</string>\n", k, xml_escape_plist(v));
        }
    };
    emit_dt("DTPlatformName", dt.platform_name);
    emit_dt("DTPlatformVersion", dt.platform_version);
    emit_dt("DTPlatformBuild", dt.platform_build);
    emit_dt("DTSDKName", dt.sdk_name);
    emit_dt("DTSDKBuild", dt.sdk_build);
    emit_dt("DTXcode", dt.xcode_version);
    emit_dt("DTXcodeBuild", dt.xcode_build);

    bundle_detail::emit_url_types_plist(plist, plan, effective_bundle_id);

    if (!partial_icon_xml.empty()) {
        plist << partial_icon_xml;
    } else if (has_fallback_icon && !bundle_detail::plan_has_plist_extra(plan, "CFBundleIconFiles")) {
        plist << "    <key>CFBundleIconFiles</key>\n    <array>\n        <string>AppIcon60x60</string>\n    </array>\n";
    }

    bundle_detail::emit_plist_extras(plist, plan);
    plist << "</dict>\n</plist>\n";
    return plist.str();
}

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

    std::filesystem::path raster_src = bundle_detail::rasterize_svg_icon_to_png(icon_src, cache_dir);

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

struct IosIconResult {
    std::string partial_plist_xml{};
    bool has_fallback_icon = false;
};

[[nodiscard]] inline auto ensure_ios_appicon(
    const std::filesystem::path& icon_src,
    const std::filesystem::path& app_bundle,
    const BuildPlan& plan
) -> IosIconResult {
    IosIconResult out;
#if defined(__APPLE__)
    std::error_code ec;
    if (!std::filesystem::exists(icon_src, ec)) {
        return out;
    }

    cfire::detail::SecureTempDir stage("cfire-xcassets");
    std::filesystem::path raster_src = bundle_detail::rasterize_svg_icon_to_png(icon_src, stage.path());
    if (!std::filesystem::exists(raster_src, ec)) {
        return out;
    }

    auto xcassets_dir = stage.path() / "Assets.xcassets";
    auto appiconset_dir = xcassets_dir / "AppIcon.appiconset";
    std::filesystem::create_directories(appiconset_dir, ec);
    (void)bundle_detail::write_file_text(xcassets_dir / "Contents.json", "{\n  \"info\" : {\n    \"author\" : \"xcode\",\n    \"version\" : 1\n  }\n}\n");

    struct IosScale { int size; const char* filename; };
    const IosScale scales[] = {
        {120, "icon_120.png"},
        {180, "icon_180.png"},
        {152, "icon_152.png"},
        {167, "icon_167.png"},
        {1024, "icon_1024.png"},
    };
    const std::string q_src = bridge::shell_quote(raster_src.string());
    bool all_resized = true;
    for (const auto& sc : scales) {
        auto dst = appiconset_dir / sc.filename;
        int rc = 0;
        std::string sips_cmd = std::format("/usr/bin/sips -s format png -z {} {} {} --out {} >/dev/null 2>&1",
            sc.size, sc.size, q_src, bridge::shell_quote(dst.string()));
        (void)bridge::run_command_capture(sips_cmd, rc);
        if (rc != 0 || !std::filesystem::exists(dst, ec)) {
            all_resized = false;
        }
    }

    const std::string appicon_json = R"({
  "images" : [
    { "filename" : "icon_120.png", "idiom" : "iphone", "scale" : "2x", "size" : "60x60" },
    { "filename" : "icon_180.png", "idiom" : "iphone", "scale" : "3x", "size" : "60x60" },
    { "filename" : "icon_152.png", "idiom" : "ipad", "scale" : "2x", "size" : "76x76" },
    { "filename" : "icon_167.png", "idiom" : "ipad", "scale" : "2x", "size" : "83.5x83.5" },
    { "filename" : "icon_1024.png", "idiom" : "ios-marketing", "scale" : "1x", "size" : "1024x1024" }
  ],
  "info" : {
    "author" : "xcode",
    "version" : 1
  }
}
)";
    (void)bundle_detail::write_file_text(appiconset_dir / "Contents.json", appicon_json);

    if (all_resized) {
        std::string plat = plan.is_ios_sim ? "iphonesimulator" : "iphoneos";
        std::string dep_target = plan.ios_min.empty() ? std::string(kDefaultIosMinVersion) : plan.ios_min;
        auto partial_plist = stage.path() / "partial.plist";
        int ac_rc = 0;
        std::string ac_cmd = std::format(
            "xcrun actool --compile {} --platform {} --minimum-deployment-target {} --app-icon AppIcon --output-partial-info-plist {} {} >/dev/null 2>&1",
            bridge::shell_quote(app_bundle.string()),
            bridge::shell_quote(plat),
            bridge::shell_quote(dep_target),
            bridge::shell_quote(partial_plist.string()),
            bridge::shell_quote(xcassets_dir.string()));
        (void)bridge::run_command_capture(ac_cmd, ac_rc);
        if (ac_rc == 0 && std::filesystem::exists(app_bundle / "Assets.car", ec)) {
            if (std::filesystem::exists(partial_plist, ec)) {
                int pl_rc = 0;
                (void)bridge::run_command_capture(
                    std::format("plutil -convert xml1 {} >/dev/null 2>&1", bridge::shell_quote(partial_plist.string())),
                    pl_rc);
                if (auto xml_txt = read_file_text(partial_plist)) {
                    out.partial_plist_xml = bundle_detail::extract_partial_plist_inner_xml(*xml_txt);
                }
            }
            if (!out.partial_plist_xml.contains("CFBundleIconName")) {
                out.partial_plist_xml += "    <key>CFBundleIconName</key>\n    <string>AppIcon</string>\n";
            }
            return out;
        }
    }

    auto fallback_120 = appiconset_dir / "icon_120.png";
    auto dest_icon = app_bundle / "AppIcon60x60@2x.png";
    if (std::filesystem::exists(fallback_120, ec)) {
        std::filesystem::copy_file(fallback_120, dest_icon, std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec && std::filesystem::exists(dest_icon, ec)) {
            out.has_fallback_icon = true;
            return out;
        }
    }
    int sips_rc = 0;
    std::string sips_cmd = std::format("/usr/bin/sips -s format png -z 120 120 {} --out {} >/dev/null 2>&1",
        q_src, bridge::shell_quote(dest_icon.string()));
    (void)bridge::run_command_capture(sips_cmd, sips_rc);
    if (sips_rc == 0 && std::filesystem::exists(dest_icon, ec)) {
        out.has_fallback_icon = true;
    }
#else
    (void)icon_src; (void)app_bundle; (void)plan;
#endif
    return out;
}

[[nodiscard]] inline auto copy_bundle_resources(
    const BuildPlan& plan,
    const std::filesystem::path& cf_file,
    const std::filesystem::path& dest_root
) -> std::expected<void, std::string> {
    std::error_code ec;
    std::filesystem::path base_dir = !plan.manifest_dir.empty()
        ? std::filesystem::path(plan.manifest_dir)
        : cf_file.parent_path();
    std::vector<std::string> res_list;
    if (plan.has_explicit_resources) {
        res_list = plan.resources;
    } else {
        auto ui_cand = base_dir / "ui";
        if (std::filesystem::exists(ui_cand, ec)) {
            res_list.push_back("ui");
        }
    }
    if (res_list.empty()) return {};

    std::filesystem::create_directories(dest_root, ec);
    for (const auto& item : res_list) {
        if (item.empty()) continue;
        std::filesystem::path src = bundle_detail::resolve_relative_project_path(item, base_dir);
        if (!std::filesystem::exists(src, ec)) {
            if (plan.has_explicit_resources) {
                return std::unexpected(std::format("declared build.cf resource path not found: '{}'", src.string()));
            }
            continue;
        }
        auto target_name = src.filename();
        if (target_name.empty() || target_name == "." || target_name == "..") {
            target_name = "resource";
        }
        auto dst = dest_root / target_name;
        if (std::filesystem::is_directory(src, ec)) {
            std::filesystem::create_directories(dst, ec);
            std::filesystem::copy(src, dst,
                std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
        } else {
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
        }
        if (ec) {
            return std::unexpected(std::format("failed copying resource '{}' to '{}': {}",
                src.string(), dst.string(), ec.message()));
        }
    }
    return {};
}

[[nodiscard]] inline auto prepare_provisioning_and_entitlements(
    const BuildPlan& plan,
    const std::filesystem::path& project_root,
    const std::filesystem::path& profile_dest,
    const std::filesystem::path& extracted_ent_tmp
) -> std::expected<std::string, std::string> {
    std::error_code ec;
    std::string resolved_entitlements;
    if (!plan.entitlements.empty()) {
        std::filesystem::path ent_p = bundle_detail::resolve_relative_project_path(plan.entitlements, project_root);
        if (!std::filesystem::exists(ent_p, ec)) {
            return std::unexpected(std::format("entitlements file not found: '{}'", ent_p.string()));
        }
#if defined(__APPLE__)
        int lint_rc = 0;
        std::string lint_out = bridge::run_command_capture(
            std::format("plutil -lint {} 2>&1", bridge::shell_quote(ent_p.string())), lint_rc);
        if (lint_rc != 0) {
            return std::unexpected(std::format("invalid entitlements plist '{}': {}", ent_p.string(), trim_bundle_ws(lint_out)));
        }
#endif
        resolved_entitlements = ent_p.string();
    }

    if (!plan.provisioning_profile.empty()) {
        std::filesystem::path prof_p = bundle_detail::resolve_relative_project_path(plan.provisioning_profile, project_root);
        if (!std::filesystem::exists(prof_p, ec)) {
            return std::unexpected(std::format("provisioning profile not found: '{}'", prof_p.string()));
        }
        if (profile_dest.has_parent_path()) {
            std::filesystem::create_directories(profile_dest.parent_path(), ec);
        }
        std::filesystem::copy_file(prof_p, profile_dest, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            return std::unexpected(std::format("failed copying provisioning profile to '{}': {}",
                profile_dest.string(), ec.message()));
        }
#if defined(__APPLE__)
        if (plan.is_ios && resolved_entitlements.empty() && !extracted_ent_tmp.empty()) {
            int cms_rc = 0;
            std::string ext_cmd = std::format(
                "security cms -D -i {} 2>/dev/null | plutil -extract Entitlements xml1 -o {} - 2>/dev/null",
                bridge::shell_quote(profile_dest.string()),
                bridge::shell_quote(extracted_ent_tmp.string()));
            (void)bridge::run_command_capture(ext_cmd, cms_rc);
            if (cms_rc == 0 && std::filesystem::exists(extracted_ent_tmp, ec)) {
                resolved_entitlements = extracted_ent_tmp.string();
            } else if (!plan.is_ios_sim) {
                return std::unexpected(std::format(
                    "failed to extract Entitlements from provisioning profile '{}'", prof_p.string()));
            }
        }
#else
        (void)extracted_ent_tmp;
#endif
    }
    return resolved_entitlements;
}

[[nodiscard]] inline auto codesign_apple_bundle(
    const std::filesystem::path& app_bundle,
    const std::filesystem::path& frameworks_dir,
    const std::string& sign_identity,
    const std::string& entitlements_path,
    bool is_ios,
    bool is_ios_sim
) -> std::expected<void, std::string> {
    std::string effective_id = sign_identity.empty() ? "-" : sign_identity;
    const bool is_device_ios = is_ios && !is_ios_sim;
    if (is_device_ios && (effective_id == "-")) {
        return std::unexpected(
            "iOS device builds require a non-adhoc code signing identity (`SignIdentity` in build.cf or `--sign <identity>`)");
    }

#if defined(__APPLE__)
    const bool strict_sign = is_device_ios || (effective_id != "-");
    std::error_code ec;
    auto run_codesign = [&](const std::filesystem::path& target, bool with_entitlements) -> std::expected<void, std::string> {
        std::string cmd = std::format("codesign --force --sign {}", bridge::shell_quote(effective_id));
        if (with_entitlements && !entitlements_path.empty()) {
            cmd += std::format(" --entitlements {}", bridge::shell_quote(entitlements_path));
        }
        cmd += std::format(" {} 2>&1", bridge::shell_quote(target.string()));
        int rc = 0;
        std::string out = bridge::run_command_capture(cmd, rc);
        if (rc != 0 && strict_sign) {
            return std::unexpected(std::format("codesign failed for '{}':\n{}", target.string(), trim_bundle_ws(out)));
        }
        return {};
    };

    if (!frameworks_dir.empty() && std::filesystem::exists(frameworks_dir, ec) && std::filesystem::is_directory(frameworks_dir, ec)) {
        std::vector<std::filesystem::path> nested_items;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(frameworks_dir, ec)) {
            auto ext = entry.path().extension().string();
            if (ext == ".dylib" || ext == ".so" || ext == ".framework" || ext == ".appex" || ext == ".xpc") {
                nested_items.push_back(entry.path());
            }
        }
        // Inside-out signing: sign deepest embedded binaries/bundles before enclosing .framework/.app
        std::sort(nested_items.begin(), nested_items.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
            return a.string().size() > b.string().size();
        });
        for (const auto& item : nested_items) {
            if (auto r = run_codesign(item, false); !r) return r;
        }
    }

    return run_codesign(app_bundle, true);
#else
    (void)app_bundle; (void)frameworks_dir; (void)entitlements_path;
    return {};
#endif
}

[[nodiscard]] inline auto package_ios_ipa(
    const std::filesystem::path& app_bundle,
    const std::filesystem::path& ipa_out
) -> std::expected<std::filesystem::path, std::string> {
#if defined(__APPLE__)
    std::error_code ec;
    if (!std::filesystem::exists(app_bundle, ec) || !std::filesystem::is_directory(app_bundle, ec)) {
        return std::unexpected(std::format("iOS app bundle not found for .ipa packaging: {}", app_bundle.string()));
    }
    cfire::detail::SecureTempDir stage("cfire-ipa");
    auto payload_dir = stage.path() / "Payload";
    auto staged_app = payload_dir / app_bundle.filename();
    std::filesystem::create_directories(payload_dir, ec);
    std::filesystem::copy(app_bundle, staged_app,
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return std::unexpected(std::format("failed staging Payload for '{}': {}", ipa_out.string(), ec.message()));
    }
    if (ipa_out.has_parent_path()) {
        std::filesystem::create_directories(ipa_out.parent_path(), ec);
    }
    auto abs_ipa = std::filesystem::absolute(ipa_out, ec);
    std::filesystem::remove(abs_ipa, ec);
    int rc = 0;
    std::string cmd = std::format("cd {} && /usr/bin/ditto -c -k --sequesterRsrc --keepParent Payload {} 2>&1",
        bridge::shell_quote(stage.path().string()),
        bridge::shell_quote(abs_ipa.string()));
    std::string out = bridge::run_command_capture(cmd, rc);
    if (rc != 0 || !std::filesystem::exists(abs_ipa, ec)) {
        return std::unexpected(std::format("ditto failed creating .ipa '{}':\n{}", abs_ipa.string(), trim_bundle_ws(out)));
    }
    return abs_ipa;
#else
    (void)app_bundle; (void)ipa_out;
    return std::unexpected("iOS .ipa packaging requires macOS");
#endif
}

[[nodiscard]] inline auto package_apple_app_bundles(
    const std::filesystem::path& cf_file,
    const std::filesystem::path& out_bin,
    BuildPlan& plan
) -> std::expected<void, std::string> {
    const std::filesystem::path project_root = !plan.manifest_dir.empty()
        ? std::filesystem::path(plan.manifest_dir)
        : cf_file.parent_path();

    if (!plan.macos_app_bundle.empty()) {
        std::error_code ec;
        auto app_bundle = std::filesystem::path(plan.macos_app_bundle);
        auto macos_dir = app_bundle / "Contents" / "MacOS";
        auto resources_dir = app_bundle / "Contents" / "Resources";
        std::filesystem::create_directories(macos_dir, ec);
        std::string bin_name = out_bin.extension() == ".app" ? out_bin.stem().string() : out_bin.filename().string();
        auto bundle_bin = macos_dir / bin_name;
        if (out_bin != bundle_bin && std::filesystem::is_regular_file(out_bin, ec)) {
            std::filesystem::copy_file(out_bin, bundle_bin, std::filesystem::copy_options::overwrite_existing, ec);
        }
        std::filesystem::permissions(bundle_bin,
            std::filesystem::perms::owner_all | std::filesystem::perms::group_read | std::filesystem::perms::group_exec |
            std::filesystem::perms::others_read | std::filesystem::perms::others_exec, ec);

        bool has_icon = false;
        if (!plan.icon_path.empty()) {
            std::filesystem::path icon_p = bundle_detail::resolve_relative_project_path(plan.icon_path, project_root);
            std::filesystem::create_directories(resources_dir, ec);
            auto icns_res = ensure_macos_icns(icon_p, app_bundle.parent_path() / ".cfire" / "cache" / "icons");
            if (icns_res) {
                auto dest_icns = resources_dir / "AppIcon.icns";
                std::filesystem::copy_file(*icns_res, dest_icns, std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec) has_icon = true;
            }
        }

        if (auto wr = bundle_detail::write_file_text(app_bundle / "Contents" / "Info.plist",
                generate_macos_info_plist(plan, bin_name, has_icon)); !wr) {
            return std::unexpected(wr.error());
        }

        if (auto r_res = copy_bundle_resources(plan, cf_file, resources_dir); !r_res) {
            return r_res;
        }

        cfire::detail::SecureTempDir sign_tmp("cfire-mac-sign");
        auto ent_res = prepare_provisioning_and_entitlements(
            plan, project_root, app_bundle / "Contents" / "embedded.provisionprofile",
            sign_tmp.path() / "entitlements.plist");
        if (!ent_res) return std::unexpected(ent_res.error());

        if (auto cs = codesign_apple_bundle(
                app_bundle, app_bundle / "Contents" / "Frameworks",
                plan.sign_identity, *ent_res, false, false); !cs) {
            return cs;
        }
    }

    if (!plan.ios_app_bundle.empty()) {
        std::error_code ec;
        auto app_bundle = std::filesystem::path(plan.ios_app_bundle);
        std::filesystem::create_directories(app_bundle, ec);
        std::string bin_name = (out_bin.extension() == ".app" || out_bin.extension() == ".ipa")
            ? app_bundle.stem().string()
            : out_bin.filename().string();
        auto bundle_bin = app_bundle / bin_name;
        if (out_bin != bundle_bin && std::filesystem::is_regular_file(out_bin, ec)) {
            std::filesystem::copy_file(out_bin, bundle_bin, std::filesystem::copy_options::overwrite_existing, ec);
        }
        std::filesystem::permissions(bundle_bin,
            std::filesystem::perms::owner_all | std::filesystem::perms::group_read | std::filesystem::perms::group_exec |
            std::filesystem::perms::others_read | std::filesystem::perms::others_exec, ec);

        if (auto r_res = copy_bundle_resources(plan, cf_file, app_bundle); !r_res) {
            return r_res;
        }

        IosIconResult icon_res;
        if (!plan.icon_path.empty()) {
            std::filesystem::path icon_p = bundle_detail::resolve_relative_project_path(plan.icon_path, project_root);
            icon_res = ensure_ios_appicon(icon_p, app_bundle, plan);
        }

        AppleDtMetadata dt = query_apple_dt_metadata(plan.is_ios_sim);
        if (auto wr = bundle_detail::write_file_text(app_bundle / "Info.plist",
                generate_ios_info_plist(plan, bin_name, dt, icon_res.partial_plist_xml, icon_res.has_fallback_icon)); !wr) {
            return std::unexpected(wr.error());
        }

        auto privacy_dest = app_bundle / "PrivacyInfo.xcprivacy";
        if (!plan.privacy_manifest.empty()) {
            auto pm_src = bundle_detail::resolve_relative_project_path(plan.privacy_manifest, project_root);
            if (!std::filesystem::exists(pm_src, ec)) {
                return std::unexpected(std::format("PrivacyManifest file not found: '{}'", pm_src.string()));
            }
            std::filesystem::copy_file(pm_src, privacy_dest, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                return std::unexpected(std::format("failed copying PrivacyManifest '{}': {}", pm_src.string(), ec.message()));
            }
        } else {
            if (auto wr = bundle_detail::write_file_text(privacy_dest, generate_default_privacy_manifest_xml()); !wr) {
                return std::unexpected(wr.error());
            }
        }

        cfire::detail::SecureTempDir sign_tmp("cfire-ios-sign");
        auto ent_res = prepare_provisioning_and_entitlements(
            plan, project_root, app_bundle / "embedded.mobileprovision",
            sign_tmp.path() / "extracted_entitlements.plist");
        if (!ent_res) return std::unexpected(ent_res.error());

        if (auto cs = codesign_apple_bundle(
                app_bundle, app_bundle / "Frameworks",
                plan.sign_identity, *ent_res, plan.is_ios, plan.is_ios_sim); !cs) {
            return cs;
        }

        if (plan.emit_ipa || out_bin.extension() == ".ipa") {
            std::filesystem::path ipa_target = (out_bin.extension() == ".ipa")
                ? out_bin
                : app_bundle.parent_path() / (app_bundle.stem().string() + ".ipa");
            auto ipa_res = package_ios_ipa(app_bundle, ipa_target);
            if (!ipa_res) return std::unexpected(ipa_res.error());
            plan.ios_ipa_path = ipa_res->string();
        }
    }

    return {};
}

struct IosDeviceInfo {
    std::string identifier{};
    std::string udid{};
    std::string name{};
    std::string tunnel_state{};
    bool connected = false;
};

[[nodiscard]] inline auto parse_devicectl_devices_json(
    std::string_view json_str,
    std::string_view preferred = ""
) -> std::expected<IosDeviceInfo, std::string> {
    auto parse_obj = [&](std::size_t& pos, auto&& on_field) -> bool {
        skip_json_ws(json_str, pos);
        if (pos >= json_str.size() || json_str[pos] != '{') return false;
        ++pos;
        skip_json_ws(json_str, pos);
        if (pos < json_str.size() && json_str[pos] == '}') {
            ++pos;
            return true;
        }
        while (pos < json_str.size()) {
            skip_json_ws(json_str, pos);
            std::string key;
            if (!parse_json_string_literal(json_str, pos, key)) return false;
            skip_json_ws(json_str, pos);
            if (pos >= json_str.size() || json_str[pos] != ':') return false;
            ++pos;
            skip_json_ws(json_str, pos);
            const std::size_t before = pos;
            if (!on_field(std::string_view(key), pos)) return false;
            if (pos == before) {
                if (!skip_json_value(json_str, pos)) return false;
            }
            skip_json_ws(json_str, pos);
            if (pos >= json_str.size()) return false;
            if (json_str[pos] == '}') {
                ++pos;
                return true;
            }
            if (json_str[pos] != ',') return false;
            ++pos;
        }
        return false;
    };

    auto to_lower = [](std::string_view sv) -> std::string {
        std::string out;
        out.reserve(sv.size());
        for (char c : sv) {
            out.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c);
        }
        return out;
    };

    bool has_error = false;
    std::string err_desc;
    bool has_result = false;
    bool has_devices_arr = false;
    std::vector<IosDeviceInfo> all_devices;
    std::vector<IosDeviceInfo> connected;

    std::size_t root_pos = 0;
    skip_json_ws(json_str, root_pos);
    if (root_pos >= json_str.size() || json_str[root_pos] != '{') {
        return std::unexpected("failed to parse xcrun devicectl JSON: expected root object");
    }
    if (!parse_obj(root_pos, [&](std::string_view key, std::size_t& pos) -> bool {
        if (key == "error" && pos < json_str.size() && json_str[pos] == '{') {
            has_error = true;
            return parse_obj(pos, [&](std::string_view ek, std::size_t& epos) -> bool {
                if (ek == "userInfo" && epos < json_str.size() && json_str[epos] == '{') {
                    return parse_obj(epos, [&](std::string_view uk, std::size_t& upos) -> bool {
                        if (uk == "NSLocalizedDescription") {
                            if (upos < json_str.size() && json_str[upos] == '{') {
                                return parse_obj(upos, [&](std::string_view lk, std::size_t& lpos) -> bool {
                                    if (lk == "string" && lpos < json_str.size() && json_str[lpos] == '"') {
                                        return parse_json_string_literal(json_str, lpos, err_desc);
                                    }
                                    return true;
                                });
                            }
                            if (upos < json_str.size() && json_str[upos] == '"') {
                                return parse_json_string_literal(json_str, upos, err_desc);
                            }
                        }
                        return true;
                    });
                }
                if (ek == "message" && err_desc.empty() && epos < json_str.size() && json_str[epos] == '"') {
                    return parse_json_string_literal(json_str, epos, err_desc);
                }
                return true;
            });
        }
        if (key == "result" && pos < json_str.size() && json_str[pos] == '{') {
            has_result = true;
            return parse_obj(pos, [&](std::string_view rk, std::size_t& rpos) -> bool {
                if (rk == "devices" && rpos < json_str.size() && json_str[rpos] == '[') {
                    has_devices_arr = true;
                    ++rpos;
                    skip_json_ws(json_str, rpos);
                    if (rpos < json_str.size() && json_str[rpos] == ']') {
                        ++rpos;
                        return true;
                    }
                    while (rpos < json_str.size()) {
                        skip_json_ws(json_str, rpos);
                        if (rpos < json_str.size() && json_str[rpos] == '{') {
                            IosDeviceInfo info;
                            if (!parse_obj(rpos, [&](std::string_view dk, std::size_t& dpos) -> bool {
                                if (dk == "identifier" && dpos < json_str.size() && json_str[dpos] == '"') {
                                    return parse_json_string_literal(json_str, dpos, info.identifier);
                                }
                                if (dk == "hardwareProperties" && dpos < json_str.size() && json_str[dpos] == '{') {
                                    return parse_obj(dpos, [&](std::string_view hk, std::size_t& hpos) -> bool {
                                        if (hk == "udid" && hpos < json_str.size() && json_str[hpos] == '"') {
                                            return parse_json_string_literal(json_str, hpos, info.udid);
                                        }
                                        return true;
                                    });
                                }
                                if (dk == "deviceProperties" && dpos < json_str.size() && json_str[dpos] == '{') {
                                    return parse_obj(dpos, [&](std::string_view pk, std::size_t& ppos) -> bool {
                                        if (pk == "name" && ppos < json_str.size() && json_str[ppos] == '"') {
                                            return parse_json_string_literal(json_str, ppos, info.name);
                                        }
                                        return true;
                                    });
                                }
                                if (dk == "connectionProperties" && dpos < json_str.size() && json_str[dpos] == '{') {
                                    return parse_obj(dpos, [&](std::string_view ck, std::size_t& cpos) -> bool {
                                        if (ck == "tunnelState" && cpos < json_str.size() && json_str[cpos] == '"') {
                                            return parse_json_string_literal(json_str, cpos, info.tunnel_state);
                                        }
                                        return true;
                                    });
                                }
                                return true;
                            })) {
                                return false;
                            }
                            std::string ts_low = to_lower(info.tunnel_state);
                            info.connected = (ts_low == "connected" || ts_low == "available");
                            all_devices.push_back(info);
                            if (info.connected) {
                                connected.push_back(std::move(info));
                            }
                        } else {
                            if (!skip_json_value(json_str, rpos)) return false;
                        }
                        skip_json_ws(json_str, rpos);
                        if (rpos >= json_str.size()) return false;
                        if (json_str[rpos] == ']') {
                            ++rpos;
                            return true;
                        }
                        if (json_str[rpos] != ',') return false;
                        ++rpos;
                    }
                    return false;
                }
                return true;
            });
        }
        return true;
    })) {
        return std::unexpected("failed to parse xcrun devicectl JSON");
    }

    if (has_error) {
        return std::unexpected(err_desc.empty()
            ? std::string("xcrun devicectl returned an error")
            : std::format("xcrun devicectl error: {}", err_desc));
    }
    if (!has_result) {
        return std::unexpected("unexpected xcrun devicectl JSON: missing 'result' object");
    }
    if (!has_devices_arr) {
        return std::unexpected("unexpected xcrun devicectl JSON: missing 'result.devices' array");
    }

    if (connected.empty()) {
        return std::unexpected(std::format(
            "no connected physical iOS devices found via `xcrun devicectl` ({} disconnected/paired device(s)); "
            "connect an unlocked iPhone or iPad and trust this Mac",
            all_devices.size()));
    }

    if (!preferred.empty()) {
        std::vector<IosDeviceInfo> exact;
        std::vector<IosDeviceInfo> ci_name;
        const std::string pref_low = to_lower(preferred);
        for (const auto& d : connected) {
            if (d.identifier == preferred || d.udid == preferred || d.name == preferred) {
                exact.push_back(d);
            } else if (!d.name.empty() && to_lower(d.name) == pref_low) {
                ci_name.push_back(d);
            }
        }
        const auto& matches = !exact.empty() ? exact : ci_name;
        if (matches.size() == 1) {
            return matches[0];
        }
        if (matches.size() > 1) {
            std::string msg = std::format("multiple connected iOS devices match '{}'; specify the UDID or identifier:\n", preferred);
            for (const auto& m : matches) {
                msg += std::format("  - {} (udid={}, id={})\n", m.name, m.udid, m.identifier);
            }
            return std::unexpected(msg);
        }
        std::string msg = std::format("no connected iOS device matching '{}'. Connected devices:\n", preferred);
        for (const auto& d : connected) {
            msg += std::format("  - {} (udid={}, id={})\n", d.name, d.udid, d.identifier);
        }
        return std::unexpected(msg);
    }

    if (connected.size() == 1) {
        return connected[0];
    }

    std::string msg = "multiple connected iOS devices found; pass `--device <name|udid>`:\n";
    for (const auto& d : connected) {
        msg += std::format("  - {} (udid={}, id={})\n", d.name, d.udid, d.identifier);
    }
    return std::unexpected(msg);
}

[[nodiscard]] inline auto launch_ios_device(
    const std::filesystem::path& app_bundle,
    const std::string& bundle_id,
    const std::string& sign_identity,
    const std::string& preferred_device = "",
    const std::vector<std::string>& run_args = {}
) -> std::expected<int, std::string> {
#if defined(__APPLE__)
    if (sign_identity.empty() || sign_identity == "-") {
        return std::unexpected(
            "running on a physical iOS device (`--ios-device`) requires a non-adhoc Apple signing identity "
            "(`SignIdentity: \"Apple Development: ...\"` in build.cf or `--sign <identity>`)");
    }
    std::error_code ec;
    if (!std::filesystem::exists(app_bundle, ec)) {
        return std::unexpected(std::format("iOS app bundle not found: {}", app_bundle.string()));
    }

    cfire::detail::SecureTempDir dev_tmp("cfire-devicectl");
    auto json_out = dev_tmp.file("devices.json");
    int list_rc = 0;
    std::string list_out = bridge::run_command_capture(
        std::format("xcrun devicectl list devices --json-output {} 2>&1", bridge::shell_quote(json_out.string())),
        list_rc);
    auto json_txt = read_file_text(json_out);
    if (!json_txt || trim_bundle_ws(*json_txt).empty()) {
        return std::unexpected(std::format("`xcrun devicectl list devices` failed (rc={}):\n{}", list_rc, trim_bundle_ws(list_out)));
    }
    auto dev_res = parse_devicectl_devices_json(*json_txt, preferred_device);
    if (!dev_res) return std::unexpected(dev_res.error());
    const std::string target_id = !dev_res->udid.empty() ? dev_res->udid : dev_res->identifier;

    std::println("==> Installing {} on iOS device: {} ({})...",
        app_bundle.filename().string(), dev_res->name.empty() ? target_id : dev_res->name, target_id);
    int inst_rc = 0;
    std::string inst_out = bridge::run_command_capture(
        std::format("xcrun devicectl device install app --device {} {} 2>&1",
            bridge::shell_quote(target_id), bridge::shell_quote(app_bundle.string())),
        inst_rc);
    if (inst_rc != 0) {
        return std::unexpected(std::format("failed to install '{}' on device '{}':\n{}",
            app_bundle.string(), target_id, trim_bundle_ws(inst_out)));
    }

    std::println("==> Launching {} ({}) on device {}...",
        app_bundle.filename().string(), bundle_id, dev_res->name.empty() ? target_id : dev_res->name);
    std::string launch_cmd = std::format("xcrun devicectl device process launch --console --device {} {}",
        bridge::shell_quote(target_id), bridge::shell_quote(bundle_id));
    for (const auto& a : run_args) {
        launch_cmd += " " + bridge::shell_quote(a);
    }
    int raw_rc = std::system(launch_cmd.c_str());
    if (raw_rc == -1) return 1;
    return WIFEXITED(raw_rc) ? WEXITSTATUS(raw_rc) : (WIFSIGNALED(raw_rc) ? 128 + WTERMSIG(raw_rc) : raw_rc);
#else
    (void)app_bundle; (void)bundle_id; (void)sign_identity; (void)preferred_device; (void)run_args;
    return std::unexpected("running on a physical iOS device (`--ios-device`) requires macOS with Xcode installed.");
#endif
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
            (void)bridge::run_command_capture(std::format("xcrun simctl boot {} 2>/dev/null", bridge::shell_quote(chosen_udid)), rc);
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
