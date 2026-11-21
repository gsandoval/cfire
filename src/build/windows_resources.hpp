#pragma once
// src/build/windows_resources.hpp — Windows PE Resource & Icon Compilation

#include "../clang_bridge.hpp"
#include "cache_paths.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace cfire::build {

[[nodiscard]] inline auto resolve_windres(std::string_view target_triple) -> std::string {
    if (const char* env_rc = std::getenv("WINDRES"); env_rc && *env_rc) {
        return std::string(env_rc);
    }
    if (const char* env_rc = std::getenv("RC"); env_rc && *env_rc) {
        return std::string(env_rc);
    }
    if (!target_triple.empty()) {
        std::string prefixed = std::format("{}-windres", target_triple);
        int rc = 0;
#if defined(_WIN32)
        (void)bridge::run_command_capture(std::format("where {} >NUL 2>&1", prefixed), rc);
#else
        (void)bridge::run_command_capture(std::format("command -v {} >/dev/null 2>&1", prefixed), rc);
#endif
        if (rc == 0) return prefixed;

        if (target_triple.find("w64-mingw32") != std::string::npos) {
            std::string alt = target_triple.starts_with("x86_64") ? "x86_64-w64-mingw32-windres" : "i686-w64-mingw32-windres";
            int rc_alt = 0;
#if defined(_WIN32)
            (void)bridge::run_command_capture(std::format("where {} >NUL 2>&1", alt), rc_alt);
#else
            (void)bridge::run_command_capture(std::format("command -v {} >/dev/null 2>&1", alt), rc_alt);
#endif
            if (rc_alt == 0) return alt;
        }
    }
#if defined(_WIN32)
    return "windres.exe";
#else
    return "x86_64-w64-mingw32-windres";
#endif
}

[[nodiscard]] inline auto wrap_png_as_ico(
    const std::filesystem::path& png_path,
    const std::filesystem::path& out_ico_path
) -> bool {
    std::ifstream in(png_path, std::ios::binary);
    if (!in) return false;
    std::vector<std::uint8_t> png_data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (png_data.size() < 24) return false;

    // Check PNG signature: 0x89 0x50 0x4E 0x47 0x0D 0x0A 0x1A 0x0A
    if (png_data[0] != 0x89 || png_data[1] != 'P' || png_data[2] != 'N' || png_data[3] != 'G') {
        return false;
    }
    // IHDR width & height are 4-byte big-endian ints at offset 16 & 20
    std::uint32_t w = (static_cast<std::uint32_t>(png_data[16]) << 24) |
                      (static_cast<std::uint32_t>(png_data[17]) << 16) |
                      (static_cast<std::uint32_t>(png_data[18]) << 8)  |
                      (static_cast<std::uint32_t>(png_data[19]));
    std::uint32_t h = (static_cast<std::uint32_t>(png_data[20]) << 24) |
                      (static_cast<std::uint32_t>(png_data[21]) << 16) |
                      (static_cast<std::uint32_t>(png_data[22]) << 8)  |
                      (static_cast<std::uint32_t>(png_data[23]));

    std::uint8_t ico_w = (w >= 256) ? 0 : static_cast<std::uint8_t>(w);
    std::uint8_t ico_h = (h >= 256) ? 0 : static_cast<std::uint8_t>(h);

    std::ofstream out(out_ico_path, std::ios::binary | std::ios::trunc);
    if (!out) return false;

    // ICONDIR header (6 bytes)
    std::uint16_t reserved = 0;
    std::uint16_t type = 1; // 1 = ICO
    std::uint16_t count = 1; // 1 image
    out.write(reinterpret_cast<const char*>(&reserved), 2);
    out.write(reinterpret_cast<const char*>(&type), 2);
    out.write(reinterpret_cast<const char*>(&count), 2);

    // ICONDIRENTRY (16 bytes)
    std::uint8_t color_count = 0;
    std::uint8_t reserved_entry = 0;
    std::uint16_t planes = 1;
    std::uint16_t bit_count = 32;
    std::uint32_t bytes_in_res = static_cast<std::uint32_t>(png_data.size());
    std::uint32_t image_offset = 6 + 16; // Header (6) + 1 Entry (16) = 22

    out.write(reinterpret_cast<const char*>(&ico_w), 1);
    out.write(reinterpret_cast<const char*>(&ico_h), 1);
    out.write(reinterpret_cast<const char*>(&color_count), 1);
    out.write(reinterpret_cast<const char*>(&reserved_entry), 1);
    out.write(reinterpret_cast<const char*>(&planes), 2);
    out.write(reinterpret_cast<const char*>(&bit_count), 2);
    out.write(reinterpret_cast<const char*>(&bytes_in_res), 4);
    out.write(reinterpret_cast<const char*>(&image_offset), 4);

    out.write(reinterpret_cast<const char*>(png_data.data()), png_data.size());
    return out.good();
}

[[nodiscard]] inline auto escape_rc_string(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\"\"";
        else if (c == '\r' || c == '\n' || c == '\t') out.push_back(' ');
        else out.push_back(c);
    }
    return out;
}

[[nodiscard]] inline auto parse_win_version_quad(
    std::string_view version,
    std::string_view build_number
) -> std::array<unsigned int, 4> {
    std::array<unsigned int, 4> quad{1, 0, 0, 1};
    std::string_view v = version.empty() ? std::string_view("1.0") : version;
    std::size_t pos = 0;
    for (int i = 0; i < 3; ++i) {
        if (pos >= v.size()) {
            quad[static_cast<std::size_t>(i)] = 0;
            continue;
        }
        std::size_t dot = v.find('.', pos);
        std::string_view seg = (dot == std::string_view::npos) ? v.substr(pos) : v.substr(pos, dot - pos);
        unsigned int num = 0;
        for (char c : seg) {
            if (c >= '0' && c <= '9') {
                num = std::min<unsigned int>(65535U, num * 10U + static_cast<unsigned int>(c - '0'));
            } else {
                break;
            }
        }
        quad[static_cast<std::size_t>(i)] = num;
        if (dot == std::string_view::npos) {
            for (int j = i + 1; j < 3; ++j) quad[static_cast<std::size_t>(j)] = 0;
            break;
        }
        pos = dot + 1;
    }
    if (!build_number.empty()) {
        unsigned int bld = 0;
        bool saw_digit = false;
        for (char c : build_number) {
            if (c >= '0' && c <= '9') {
                saw_digit = true;
                bld = std::min<unsigned int>(65535U, bld * 10U + static_cast<unsigned int>(c - '0'));
            } else if (saw_digit) {
                break;
            }
        }
        if (saw_digit) quad[3] = bld;
    }
    return quad;
}

[[nodiscard]] inline auto generate_windows_rc_content(
    std::string_view ico_escaped,
    std::string_view version,
    std::string_view build_number,
    std::string_view app_name
) -> std::string {
    std::string rc;
    if (!ico_escaped.empty()) {
        rc += std::format("1 ICON \"{}\"\n", ico_escaped);
    }
    if (!version.empty() || !build_number.empty() || !app_name.empty()) {
        std::string_view eff_ver = version.empty() ? std::string_view("1.0") : version;
        std::string_view eff_name = app_name.empty() ? std::string_view("cfire application") : app_name;
        auto q = parse_win_version_quad(eff_ver, build_number.empty() ? std::string_view("1") : build_number);
        std::string file_ver_str = std::format("{}.{}.{}.{}", q[0], q[1], q[2], q[3]);
        std::string esc_desc = escape_rc_string(eff_name);
        std::string esc_fver = escape_rc_string(file_ver_str);
        std::string esc_pver = escape_rc_string(eff_ver);
        rc += std::format(
            "1 VERSIONINFO\n"
            "FILEVERSION {},{},{},{}\n"
            "PRODUCTVERSION {},{},{},{}\n"
            "FILEOS 0x40004L\n"
            "FILETYPE 0x1L\n"
            "BEGIN\n"
            "  BLOCK \"StringFileInfo\"\n"
            "  BEGIN\n"
            "    BLOCK \"040904B0\"\n"
            "    BEGIN\n"
            "      VALUE \"FileDescription\", \"{}\"\n"
            "      VALUE \"FileVersion\", \"{}\"\n"
            "      VALUE \"ProductName\", \"{}\"\n"
            "      VALUE \"ProductVersion\", \"{}\"\n"
            "    END\n"
            "  END\n"
            "  BLOCK \"VarFileInfo\"\n"
            "  BEGIN\n"
            "    VALUE \"Translation\", 0x0409, 1200\n"
            "  END\n"
            "END\n",
            q[0], q[1], q[2], q[3],
            q[0], q[1], q[2], q[3],
            esc_desc, esc_fver, esc_desc, esc_pver
        );
    }
    return rc;
}

[[nodiscard]] inline auto compile_windows_icon(
    const std::filesystem::path& icon_path,
    const std::filesystem::path& out_bin,
    std::string_view target_triple,
    std::string_view version = "",
    std::string_view build_number = "",
    std::string_view app_name = ""
) -> std::string {
    std::error_code ec;
    std::filesystem::path ico_file;
    auto out_dir = out_bin.has_parent_path() ? out_bin.parent_path() : std::filesystem::current_path(ec);
    std::filesystem::create_directories(out_dir, ec);
    auto stem = out_bin.stem().string();

    if (!icon_path.empty()) {
        if (icon_path.extension() == ".ico" && std::filesystem::exists(icon_path, ec)) {
            ico_file = icon_path;
        } else {
            auto sib_ico = icon_path;
            sib_ico.replace_extension(".ico");
            if (std::filesystem::exists(sib_ico, ec)) {
                ico_file = sib_ico;
            }
        }

        if (ico_file.empty()) {
            std::filesystem::path png_file;
            if (icon_path.extension() == ".png" && std::filesystem::exists(icon_path, ec)) {
                png_file = icon_path;
            } else {
                auto sib_png = icon_path;
                sib_png.replace_extension(".png");
                if (std::filesystem::exists(sib_png, ec)) {
                    png_file = sib_png;
                }
            }
            if (!png_file.empty()) {
                auto gen_ico = out_dir / (stem + "_cfire_icon.ico");
                if (wrap_png_as_ico(png_file, gen_ico)) {
                    ico_file = gen_ico;
                }
            }
        }
    }
    if (ico_file.empty() && version.empty() && build_number.empty() && app_name.empty()) {
        return "";
    }

    std::string windres = resolve_windres(target_triple);
    if (windres.empty()) return "";

    std::string ico_escaped;
    if (!ico_file.empty()) {
        ico_escaped = std::filesystem::absolute(ico_file, ec).string();
        for (char& c : ico_escaped) {
            if (c == '\\') c = '/';
        }
    }
    const std::string rc_content = generate_windows_rc_content(ico_escaped, version, build_number, app_name);
    if (rc_content.empty()) return "";
    const std::string rc_hash = sha256_hex(std::format(
        "v2|{}|{}|{}|{}",
        windres,
        target_triple,
        rc_content,
        ico_file.empty() ? std::string("no-icon") : file_fingerprint_string(ico_file)
    )).substr(0, 12);

    auto rc_path = out_dir / std::format("{}_cfire_icon_{}.rc", stem, rc_hash);
    auto res_obj = out_dir / std::format("{}_cfire_icon_{}.res.o", stem, rc_hash);
    if (std::filesystem::is_regular_file(res_obj, ec) && !ec && std::filesystem::file_size(res_obj, ec) > 0 && !ec) {
        return res_obj.string();
    }
    ec.clear();
    if (!std::filesystem::exists(rc_path, ec)) {
        std::ofstream rc_out(rc_path, std::ios::binary | std::ios::trunc);
        rc_out << rc_content;
    }

    static std::atomic<uint64_t> s_res_tmp_seq{0};
    auto tmp_res_obj = std::filesystem::path(std::format(
        "{}.tmp.{}.{}.o", res_obj.string(), current_pid(), s_res_tmp_seq.fetch_add(1, std::memory_order_relaxed)));
    int rc = 0;
    std::string cmd = std::format("{} \"{}\" -O coff -o \"{}\"", windres, rc_path.string(), tmp_res_obj.string());
    (void)bridge::run_command_capture(cmd, rc);
    if (rc != 0 || !std::filesystem::exists(tmp_res_obj, ec)) {
        std::filesystem::remove(tmp_res_obj, ec);
        return "";
    }
    std::filesystem::rename(tmp_res_obj, res_obj, ec);
    if (ec) {
        ec.clear();
        std::filesystem::copy_file(tmp_res_obj, res_obj, std::filesystem::copy_options::overwrite_existing, ec);
        std::error_code rm_ec;
        std::filesystem::remove(tmp_res_obj, rm_ec);
        if (ec) return "";
    }
    return res_obj.string();
}

} // namespace cfire::build
