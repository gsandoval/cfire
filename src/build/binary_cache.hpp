#pragma once
// src/build/binary_cache.hpp — Content-addressed native binary cache with atomic restore & LRU eviction

#include "cache_paths.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace cfire::build {

struct BinaryCacheMeta {
    bool used_pch = false;
    std::string obj_fp;
};

class BinaryCache {
public:
    static constexpr uint64_t kDefaultMaxBytes = 1024ULL * 1024ULL * 1024ULL; // 1 GiB

    static std::filesystem::path dir() {
        return cache_subdir("bin");
    }

    static uint64_t configured_max_bytes() {
        if (const char* env_mb = std::getenv("CFIRE_BIN_CACHE_MAX_MB"); env_mb && *env_mb) {
            long long mb = std::atoll(env_mb);
            if (mb <= 0) return 0;
            return static_cast<uint64_t>(mb) * 1024ULL * 1024ULL;
        }
        return kDefaultMaxBytes;
    }

    static std::string compute_key(
        const std::string& cpp_code,
        const std::string& compiler_fp,
        const std::string& opt_flags,
        const std::vector<std::string>& cflags,
        const std::vector<std::string>& ldflags,
        const std::string& linker_flag,
        const std::string& target_triple,
        const std::string& runtime_fp,
        const std::string& extra_inputs_fp
    ) {
        detail::Sha256State st;
        auto feed = [&](std::string_view tag, std::string_view val) {
            st.update(reinterpret_cast<const uint8_t*>(tag.data()), tag.size());
            const char sep1 = ':';
            st.update(reinterpret_cast<const uint8_t*>(&sep1), 1);
            st.update(reinterpret_cast<const uint8_t*>(val.data()), val.size());
            const char sep2 = '\0';
            st.update(reinterpret_cast<const uint8_t*>(&sep2), 1);
        };
        feed("schema", "v1");
        feed("compiler", compiler_fp);
        feed("opt", opt_flags);
        feed("ld_flag", linker_flag);
        feed("target", target_triple);
        feed("runtime", runtime_fp);
        feed("extra", extra_inputs_fp);
        for (const auto& cf : cflags) feed("cf", cf);
        for (const auto& lf : ldflags) feed("lf", lf);
        feed("cpp", cpp_code);
        return st.final_hex();
    }

    static bool read_meta(const std::string& key, BinaryCacheMeta& meta_out) {
        meta_out = BinaryCacheMeta{};
        if (key.empty()) return false;
        std::string raw_meta;
        auto cached_meta = dir() / (key + ".meta");
        if (!read_text_file(cached_meta, raw_meta)) return false;
        std::size_t pos = 0;
        while (pos < raw_meta.size()) {
            std::size_t nl = raw_meta.find('\n', pos);
            std::string_view line = (nl == std::string::npos)
                ? std::string_view(raw_meta).substr(pos)
                : std::string_view(raw_meta).substr(pos, nl - pos);
            pos = (nl == std::string::npos) ? raw_meta.size() : nl + 1;
            if (line == "used_pch=1") {
                meta_out.used_pch = true;
            } else if (line.starts_with("obj_fp=")) {
                meta_out.obj_fp = std::string(line.substr(7));
            }
        }
        return true;
    }

    static bool try_restore(
        const std::string& key,
        const std::filesystem::path& out_bin,
        BinaryCacheMeta* meta_out = nullptr
    ) {
        if (key.empty() || out_bin.empty()) return false;
        std::error_code ec;
        auto cached_bin = dir() / (key + ".bin");
        if (!std::filesystem::exists(cached_bin, ec) || ec) return false;
        auto sz = std::filesystem::file_size(cached_bin, ec);
        if (ec || sz == 0) return false;

        if (out_bin.has_parent_path()) {
            std::filesystem::create_directories(out_bin.parent_path(), ec);
            if (ec) return false;
        }

        static std::atomic<uint64_t> restore_seq{0};
        uint64_t seq = restore_seq.fetch_add(1, std::memory_order_relaxed);
        auto tmp = std::filesystem::path(
            out_bin.string() + std::format(".tmp.{}.{}", current_pid(), seq)
        );
        std::filesystem::copy_file(
            cached_bin, tmp,
            std::filesystem::copy_options::overwrite_existing, ec
        );
        if (ec) {
            std::filesystem::remove(tmp, ec);
            return false;
        }
#if !defined(_WIN32)
        auto exe_perms = std::filesystem::perms::owner_all |
                         std::filesystem::perms::group_read | std::filesystem::perms::group_exec |
                         std::filesystem::perms::others_read | std::filesystem::perms::others_exec;
        std::filesystem::permissions(tmp, exe_perms, std::filesystem::perm_options::replace, ec);
        ec.clear();
#endif
        std::filesystem::rename(tmp, out_bin, ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
            return false;
        }

        // Update mtime for LRU tracking
        std::filesystem::last_write_time(cached_bin, std::filesystem::file_time_type::clock::now(), ec);

        if (meta_out) {
            (void)read_meta(key, *meta_out);
        }
        return true;
    }

    static void store(
        const std::string& key,
        const std::filesystem::path& built_bin,
        const BinaryCacheMeta& meta,
        uint64_t max_bytes = UINT64_MAX
    ) {
        if (key.empty() || built_bin.empty()) return;
        std::error_code ec;
        if (!std::filesystem::exists(built_bin, ec) || ec) return;
        uint64_t bin_sz = std::filesystem::file_size(built_bin, ec);
        if (ec || bin_sz == 0) return;

        uint64_t budget = (max_bytes == UINT64_MAX) ? configured_max_bytes() : max_bytes;
        if (budget == 0) return;

        auto cache_d = dir();
        auto cached_bin = cache_d / (key + ".bin");
        auto cached_meta = cache_d / (key + ".meta");

        static std::atomic<uint64_t> store_seq{0};
        uint64_t seq = store_seq.fetch_add(1, std::memory_order_relaxed);
        auto tmp_bin = std::filesystem::path(
            cached_bin.string() + std::format(".tmp.{}.{}", current_pid(), seq)
        );
        std::filesystem::copy_file(
            built_bin, tmp_bin,
            std::filesystem::copy_options::overwrite_existing, ec
        );
        if (ec) {
            std::filesystem::remove(tmp_bin, ec);
            return;
        }
#if !defined(_WIN32)
        auto exe_perms = std::filesystem::perms::owner_all |
                         std::filesystem::perms::group_read | std::filesystem::perms::group_exec |
                         std::filesystem::perms::others_read | std::filesystem::perms::others_exec;
        std::filesystem::permissions(tmp_bin, exe_perms, std::filesystem::perm_options::replace, ec);
        ec.clear();
#endif
        std::filesystem::rename(tmp_bin, cached_bin, ec);
        if (ec) {
            std::filesystem::remove(tmp_bin, ec);
            return;
        }
        std::filesystem::last_write_time(cached_bin, std::filesystem::file_time_type::clock::now(), ec);

        atomic_write_text_file(
            cached_meta,
            std::format("used_pch={}\nobj_fp={}\n", meta.used_pch ? 1 : 0, meta.obj_fp)
        );

        evict_to_budget(budget, key);
    }

    static void evict_to_budget(uint64_t max_bytes, const std::string& preserve_key = "") {
        auto cache_d = dir();
        std::error_code ec;
        if (!std::filesystem::exists(cache_d, ec) || ec) return;

        struct Entry {
            std::string stem;
            std::filesystem::path bin_path;
            std::filesystem::path meta_path;
            uint64_t size = 0;
            int64_t mtime_ns = 0;
        };
        std::vector<Entry> items;
        uint64_t total_size = 0;

        for (const auto& de : std::filesystem::directory_iterator(cache_d, ec)) {
            if (ec) break;
            if (!de.is_regular_file(ec)) continue;
            if (de.path().extension() != ".bin") continue;
            uint64_t sz = de.file_size(ec);
            if (ec) continue;
            auto ft = de.last_write_time(ec);
            int64_t ns = ec ? 0 : std::chrono::duration_cast<std::chrono::nanoseconds>(
                ft.time_since_epoch()
            ).count();
            std::string stem = de.path().stem().string();
            items.push_back(Entry{
                stem,
                de.path(),
                cache_d / (stem + ".meta"),
                sz,
                ns
            });
            total_size += sz;
        }

        if (total_size <= max_bytes) return;

        std::sort(items.begin(), items.end(), [](const Entry& a, const Entry& b) {
            return a.mtime_ns < b.mtime_ns;
        });

        for (const auto& item : items) {
            if (total_size <= max_bytes) break;
            if (!preserve_key.empty() && item.stem == preserve_key) continue;
            std::filesystem::remove(item.bin_path, ec);
            std::filesystem::remove(item.meta_path, ec);
            if (total_size >= item.size) total_size -= item.size;
            else total_size = 0;
        }
    }
};

} // namespace cfire::build
