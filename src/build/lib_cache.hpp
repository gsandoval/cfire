#pragma once
// src/build/lib_cache.hpp — Per-Target Cached Amalgamation Archives (sqlite3 & simdjson, CF-203)

#include "../clang_bridge.hpp"
#include "cache_paths.hpp"
#include "manifest.hpp"
#include "pch.hpp"
#include "toolchain_config.hpp"

#include <atomic>
#include <cctype>
#include <chrono>
#include <expected>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef CFIRE_INFRA_INLINE
#ifdef CFIRE_SPLIT_TUS
#define CFIRE_INFRA_INLINE
#else
#define CFIRE_INFRA_INLINE inline
#endif
#endif

namespace cfire::build {

struct VendoredArchiveSpec {
    std::string_view name;
    std::string_view subdir;
    std::string_view source_file;
    std::string_view header_file;
    std::string_view archive_file;
    bool is_cpp = false;
};

[[nodiscard]] CFIRE_INFRA_INLINE auto compute_vendored_archive_path(
    const std::filesystem::path& runtime_dir,
    const BuildPlan& plan,
    const VendoredArchiveSpec& spec,
    std::string& out_compiler,
    std::string& out_ar,
    std::vector<std::string>& out_cflags,
    std::filesystem::path& out_src,
    std::filesystem::path& out_hdr
) -> std::expected<std::filesystem::path, std::string>;

[[nodiscard]] CFIRE_INFRA_INLINE auto ensure_vendored_sqlite3_archive(
    const std::filesystem::path& runtime_dir,
    const BuildPlan& plan
) -> std::expected<std::filesystem::path, std::string>;

[[nodiscard]] CFIRE_INFRA_INLINE auto ensure_vendored_simdjson_archive(
    const std::filesystem::path& runtime_dir,
    const BuildPlan& plan
) -> std::expected<std::filesystem::path, std::string>;

#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_TU_INFRA_IMPL)

namespace detail {

struct LibDirLockGuard {
    std::filesystem::path lock_dir;
    bool owned = false;

    explicit LibDirLockGuard(const std::filesystem::path& archive_path)
        : lock_dir(std::filesystem::path(archive_path.string() + ".lock")) {
        std::error_code ec;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
        while (std::chrono::steady_clock::now() < deadline) {
            if (std::filesystem::create_directory(lock_dir, ec) && !ec) {
                owned = true;
                return;
            }
            if (std::filesystem::exists(lock_dir, ec)) {
                auto wt = std::filesystem::last_write_time(lock_dir, ec);
                if (!ec && std::filesystem::file_time_type::clock::now() - wt > std::chrono::seconds(180)) {
                    std::filesystem::remove_all(lock_dir, ec);
                    continue;
                }
            }
            if (std::filesystem::exists(archive_path, ec) && !ec &&
                std::filesystem::file_size(archive_path, ec) > 0 && !ec) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    }

    ~LibDirLockGuard() {
        if (owned) {
            std::error_code ec;
            std::filesystem::remove_all(lock_dir, ec);
        }
    }
};

[[nodiscard]] inline bool ios_triple_has_version(std::string_view triple) noexcept {
    auto pos = triple.find("-apple-ios");
    if (pos == std::string_view::npos) return false;
    pos += std::string_view("-apple-ios").size();
    return pos < triple.size() && std::isdigit(static_cast<unsigned char>(triple[pos]));
}

[[nodiscard]] inline auto ensure_vendored_amalgamation_archive(
    const std::filesystem::path& runtime_dir,
    const BuildPlan& plan,
    const VendoredArchiveSpec& spec
) -> std::expected<std::filesystem::path, std::string> {
    std::string compiler;
    std::string ar_cmd;
    std::vector<std::string> cflags;
    std::filesystem::path src_path;
    std::filesystem::path hdr_path;
    auto ar_res = compute_vendored_archive_path(
        runtime_dir, plan, spec, compiler, ar_cmd, cflags, src_path, hdr_path);
    if (!ar_res) return std::unexpected(ar_res.error());
    std::filesystem::path archive_path = *ar_res;

    std::error_code ec;
    if (std::filesystem::is_regular_file(archive_path, ec) && !ec &&
        std::filesystem::file_size(archive_path, ec) > 0 && !ec) {
        return archive_path;
    }

    auto cache_root = user_libs_cache_dir();
    std::filesystem::permissions(
        cache_root, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ec);
    ec.clear();
    std::filesystem::create_directories(archive_path.parent_path(), ec);
    if (ec) {
        return std::unexpected(std::format(
            "failed to create library cache directory '{}': {}", archive_path.parent_path().string(), ec.message()));
    }
    std::filesystem::permissions(
        archive_path.parent_path(), std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ec);
    ec.clear();

    LibDirLockGuard lock(archive_path);
    if (std::filesystem::is_regular_file(archive_path, ec) && !ec &&
        std::filesystem::file_size(archive_path, ec) > 0 && !ec) {
        return archive_path;
    }

    static std::atomic<uint64_t> lib_tmp_seq{0};
    uint64_t tmp_id = lib_tmp_seq.fetch_add(1, std::memory_order_relaxed);
    std::filesystem::path tmp_obj(std::format("{}.tmp.{}.{}.o", archive_path.string(), current_pid(), tmp_id));
    std::filesystem::path tmp_ar(std::format("{}.tmp.{}.{}.a", archive_path.string(), current_pid(), tmp_id));

    std::string cc_cmd = compiler;
    for (const auto& fl : cflags) {
        cc_cmd += " ";
        cc_cmd += fl;
    }
    cc_cmd += std::format(" -I\"{}\" -c \"{}\" -o \"{}\"",
        src_path.parent_path().string(), src_path.string(), tmp_obj.string());

    int cc_rc = 0;
    std::string cc_out = bridge::run_command_capture(cc_cmd, cc_rc);
    if (cc_rc != 0) {
        std::filesystem::remove(tmp_obj, ec);
        return std::unexpected(std::format(
            "failed to compile vendored '{}' amalgamation:\n{}\nCommand: {}",
            spec.name, cc_out, cc_cmd));
    }

    std::string ar_full_cmd = std::format("{} rcs \"{}\" \"{}\"", ar_cmd, tmp_ar.string(), tmp_obj.string());
    int ar_rc = 0;
    std::string ar_out = bridge::run_command_capture(ar_full_cmd, ar_rc);
    std::filesystem::remove(tmp_obj, ec);
    if (ar_rc != 0) {
        std::filesystem::remove(tmp_ar, ec);
        return std::unexpected(std::format(
            "failed to archive vendored '{}' static library:\n{}\nCommand: {}",
            spec.name, ar_out, ar_full_cmd));
    }

    std::filesystem::permissions(
        tmp_ar,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
        std::filesystem::perms::group_read | std::filesystem::perms::others_read,
        std::filesystem::perm_options::replace,
        ec);
    ec.clear();

    std::filesystem::rename(tmp_ar, archive_path, ec);
    if (ec) {
        if (std::filesystem::is_regular_file(archive_path, ec) &&
            std::filesystem::file_size(archive_path, ec) > 0) {
            std::filesystem::remove(tmp_ar, ec);
            return archive_path;
        }
        std::filesystem::remove(tmp_ar, ec);
        return std::unexpected(std::format(
            "failed to commit vendored '{}' archive '{}': {}",
            spec.name, archive_path.string(), ec.message()));
    }
    return archive_path;
}

} // namespace detail

[[nodiscard]] CFIRE_INFRA_INLINE auto compute_vendored_archive_path(
    const std::filesystem::path& runtime_dir,
    const BuildPlan& plan,
    const VendoredArchiveSpec& spec,
    std::string& out_compiler,
    std::string& out_ar,
    std::vector<std::string>& out_cflags,
    std::filesystem::path& out_src,
    std::filesystem::path& out_hdr
) -> std::expected<std::filesystem::path, std::string> {
    auto vendor_dir = runtime_dir / "vendor" / std::string(spec.subdir);
    out_src = vendor_dir / std::string(spec.source_file);
    out_hdr = vendor_dir / std::string(spec.header_file);

    std::error_code ec;
    if (!std::filesystem::is_regular_file(out_src, ec) || !std::filesystem::is_regular_file(out_hdr, ec)) {
        return std::unexpected(std::format(
            "vendored '{}' amalgamation files not found under '{}'", spec.name, vendor_dir.string()));
    }

    bool src_ok = false;
    bool hdr_ok = false;
    std::string src_sha = sha256_file_hex(out_src, &src_ok);
    std::string hdr_sha = sha256_file_hex(out_hdr, &hdr_ok);
    if (!src_ok || !hdr_ok) {
        return std::unexpected(std::format(
            "failed to hash vendored '{}' amalgamation under '{}'", spec.name, vendor_dir.string()));
    }

    const bool is_wasm = is_wasm_triple(plan.target_triple);
    const bool is_win = is_windows_triple(plan.target_triple);
    const bool is_apple = plan.is_ios || is_apple_triple(plan.target_triple);

    if (is_wasm) {
        if (spec.is_cpp) {
            out_compiler = plan.emscripten_compiler.empty()
                ? resolve_emscripten_compiler_cfg()
                : plan.emscripten_compiler;
        } else {
            out_compiler = resolve_emscripten_c_compiler_cfg();
            if (out_compiler.empty()) {
                std::string emxx = plan.emscripten_compiler.empty()
                    ? resolve_emscripten_compiler_cfg()
                    : plan.emscripten_compiler;
                if (!emxx.empty()) out_compiler = emxx + " -x c";
            }
        }
        if (out_compiler.empty()) {
            return std::unexpected(
                "WebAssembly target requires Emscripten (emcc/em++); set EMSDK or CFIRE_EMXX");
        }
        out_ar = resolve_archiver("wasm32-unknown-emscripten");
    } else {
        out_compiler = spec.is_cpp
            ? resolve_cxx_compiler(plan.target_triple)
            : resolve_c_compiler(plan.target_triple);
        out_ar = resolve_archiver(plan.target_triple);
    }

    out_cflags.clear();
    if (is_wasm) {
        if (spec.is_cpp) {
            out_cflags = {"-std=c++2b", "-O2", "-DNDEBUG", "-sWASM=1", "-msimd128"};
        } else {
            out_cflags = {"-std=c17", "-O2", "-DNDEBUG", "-sWASM=1",
                          "-DSQLITE_OMIT_LOAD_EXTENSION=1", "-DSQLITE_THREADSAFE=0"};
        }
    } else {
        if (spec.is_cpp) {
            out_cflags = {"-std=c++2b", "-O2", "-DNDEBUG"};
            if (!is_win) out_cflags.push_back("-fPIC");
            if (!is_win && !is_apple) out_cflags.push_back("-pthread");
        } else {
            out_cflags = {"-std=c17", "-O2", "-DNDEBUG",
                          "-DSQLITE_DQS=0", "-DSQLITE_ENABLE_FTS5=1",
                          "-DSQLITE_ENABLE_JSON1=1", "-DSQLITE_ENABLE_RTREE=1",
                          "-DSQLITE_ENABLE_MATH_FUNCTIONS=1"};
            if (is_win) {
                out_cflags.push_back("-DSQLITE_OS_WIN=1");
                out_cflags.push_back("-DSQLITE_THREADSAFE=1");
            } else {
                out_cflags.push_back("-fPIC");
                out_cflags.push_back("-DSQLITE_THREADSAFE=1");
            }
        }
        if (!plan.target_triple.empty() && !compiler_is_gcc_driver(out_compiler)) {
            out_cflags.push_back("--target=" + plan.target_triple);
        }
        if (!plan.sysroot.empty()) {
            if (is_apple) {
                out_cflags.push_back("-isysroot");
                out_cflags.push_back("\"" + plan.sysroot + "\"");
            } else {
                out_cflags.push_back("--sysroot=\"" + plan.sysroot + "\"");
            }
        }
        if (plan.is_ios && !detail::ios_triple_has_version(plan.target_triple)) {
            out_cflags.push_back(plan.is_ios_sim
                ? "-mios-simulator-version-min=17.0"
                : "-miphoneos-version-min=17.0");
        }
        std::string macos_min = plan.macos_min.empty()
            ? resolve_effective_macos_min(plan.target_triple, plan.is_ios, is_wasm)
            : plan.macos_min;
        if (!macos_min.empty() && is_macos_target(plan.target_triple, plan.is_ios, is_wasm)) {
            out_cflags.push_back("-mmacosx-version-min=" + macos_min);
        }
    }

    detail::Sha256State st;
    auto feed = [&](std::string_view tag, std::string_view val) {
        st.update(reinterpret_cast<const uint8_t*>(tag.data()), tag.size());
        const char sep1 = ':';
        const char sep2 = '\0';
        st.update(reinterpret_cast<const uint8_t*>(&sep1), 1);
        st.update(reinterpret_cast<const uint8_t*>(val.data()), val.size());
        st.update(reinterpret_cast<const uint8_t*>(&sep2), 1);
    };
    feed("schema", "cf203-v1");
    feed("lib", spec.name);
    feed("src_sha", src_sha);
    feed("hdr_sha", hdr_sha);
    feed("compiler", compiler_binary_fingerprint(out_compiler));
    feed("ar", out_ar);
    feed("target", plan.target_triple);
    feed("sysroot", plan.sysroot);
    feed("macos_min", plan.macos_min);
    for (const auto& f : out_cflags) {
        feed("cf", f);
    }

    std::string key = st.final_hex();
    return user_libs_cache_dir() / key / std::string(spec.archive_file);
}

[[nodiscard]] CFIRE_INFRA_INLINE auto ensure_vendored_sqlite3_archive(
    const std::filesystem::path& runtime_dir,
    const BuildPlan& plan
) -> std::expected<std::filesystem::path, std::string> {
    static constexpr VendoredArchiveSpec kSqliteSpec{
        "sqlite3", "sqlite3", "sqlite3.c", "sqlite3.h", "libcfire_sqlite3.a", false
    };
    return detail::ensure_vendored_amalgamation_archive(runtime_dir, plan, kSqliteSpec);
}

[[nodiscard]] CFIRE_INFRA_INLINE auto ensure_vendored_simdjson_archive(
    const std::filesystem::path& runtime_dir,
    const BuildPlan& plan
) -> std::expected<std::filesystem::path, std::string> {
    static constexpr VendoredArchiveSpec kSimdjsonSpec{
        "simdjson", "simdjson", "simdjson.cpp", "simdjson.h", "libcfire_simdjson.a", true
    };
    return detail::ensure_vendored_amalgamation_archive(runtime_dir, plan, kSimdjsonSpec);
}

#endif // !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_TU_INFRA_IMPL)

} // namespace cfire::build

#undef CFIRE_INFRA_INLINE
