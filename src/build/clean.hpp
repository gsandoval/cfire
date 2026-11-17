#pragma once
// src/build/clean.hpp — Safe, marker-guarded project & user-cache cleaner (`cfire clean`)

#include "manifest.hpp"
#include "probe_cache.hpp"
#include "toolchain_layout.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace cfire::build {

struct CleanRemovedEntry {
    std::string path{};
    std::uintmax_t bytes{0};
};

struct CleanSkippedEntry {
    std::string path{};
    std::string reason{};
};

struct CleanOptions {
    bool clean_project = true;
    bool clean_cache = false;
    bool dry_run = false;
    bool json = false;
    bool has_explicit_target = false;
    std::filesystem::path target_path{};
    std::filesystem::path runtime_dir{};
};

struct CleanReport {
    bool ok = true;
    bool dry_run = false;
    std::vector<CleanRemovedEntry> removed{};
    std::vector<CleanSkippedEntry> skipped{};
    std::uintmax_t entries_removed = 0;
    std::uintmax_t bytes_freed = 0;
};

namespace clean_detail {

[[nodiscard]] inline auto is_owned_by_current_user(const std::filesystem::path& p) -> bool {
#if defined(_WIN32)
    (void)p;
    return true;
#else
    struct ::stat st{};
    if (::lstat(p.c_str(), &st) != 0) return false;
    return st.st_uid == ::getuid();
#endif
}

[[nodiscard]] inline auto path_byte_size_no_follow(const std::filesystem::path& p) -> std::uintmax_t {
    std::error_code ec;
    auto st = std::filesystem::symlink_status(p, ec);
    if (ec || !std::filesystem::exists(st)) return 0;
    if (std::filesystem::is_symlink(st)) return 0;
    if (std::filesystem::is_regular_file(st)) {
        auto sz = std::filesystem::file_size(p, ec);
        return ec ? 0 : sz;
    }
    if (!std::filesystem::is_directory(st)) return 0;

    std::uintmax_t total = 0;
    std::vector<std::filesystem::path> stack{p};
    while (!stack.empty()) {
        auto cur = std::move(stack.back());
        stack.pop_back();
        for (const auto& entry : std::filesystem::directory_iterator(cur, ec)) {
            if (ec) break;
            auto est = entry.symlink_status(ec);
            if (ec || std::filesystem::is_symlink(est)) continue;
            if (std::filesystem::is_regular_file(est)) {
                auto sz = std::filesystem::file_size(entry.path(), ec);
                if (!ec) total += sz;
            } else if (std::filesystem::is_directory(est)) {
                stack.push_back(entry.path());
            }
        }
    }
    return total;
}

inline auto remove_no_follow(
    const std::filesystem::path& p,
    bool dry_run,
    std::uintmax_t& removed_entries,
    std::uintmax_t& freed_bytes,
    std::string& err_out
) -> bool {
    std::error_code ec;
    auto st = std::filesystem::symlink_status(p, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) {
        err_out = ec.message();
        return false;
    }
    if (!std::filesystem::exists(st)) return true;

    if (std::filesystem::is_symlink(st) || !std::filesystem::is_directory(st)) {
        std::uintmax_t sz = 0;
        if (!std::filesystem::is_symlink(st) && std::filesystem::is_regular_file(st)) {
            auto fsz = std::filesystem::file_size(p, ec);
            if (!ec) sz = fsz;
        }
        removed_entries += 1;
        freed_bytes += sz;
        if (!dry_run) {
            std::filesystem::remove(p, ec);
            if (ec) {
                err_out = ec.message();
                return false;
            }
        }
        return true;
    }

    std::vector<std::filesystem::path> children;
    for (const auto& entry : std::filesystem::directory_iterator(p, ec)) {
        if (ec) {
            err_out = ec.message();
            return false;
        }
        children.push_back(entry.path());
    }
    for (const auto& child : children) {
        if (!remove_no_follow(child, dry_run, removed_entries, freed_bytes, err_out)) {
            return false;
        }
    }
    removed_entries += 1;
    if (!dry_run) {
        std::filesystem::remove(p, ec);
        if (ec) {
            err_out = ec.message();
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline auto dir_contains_cf_sources_no_follow(const std::filesystem::path& dir) -> bool {
    std::error_code ec;
    std::vector<std::filesystem::path> stack{dir};
    while (!stack.empty()) {
        auto cur = std::move(stack.back());
        stack.pop_back();
        for (const auto& entry : std::filesystem::directory_iterator(cur, ec)) {
            if (ec) continue;
            auto est = entry.symlink_status(ec);
            if (ec || std::filesystem::is_symlink(est)) continue;
            if (std::filesystem::is_regular_file(est) && entry.path().extension() == ".cf") {
                return true;
            }
            if (std::filesystem::is_directory(est)) {
                stack.push_back(entry.path());
            }
        }
    }
    return false;
}

[[nodiscard]] inline auto is_strict_descendant(
    const std::filesystem::path& parent_canon,
    const std::filesystem::path& child
) -> bool {
    std::error_code ec;
    auto st = std::filesystem::symlink_status(child, ec);
    std::filesystem::path child_canon;
    if (std::filesystem::is_symlink(st)) {
        auto pcan = std::filesystem::weakly_canonical(child.parent_path(), ec);
        if (ec || pcan.empty()) return false;
        child_canon = pcan / child.filename();
    } else {
        child_canon = std::filesystem::weakly_canonical(child, ec);
        if (ec || child_canon.empty()) return false;
    }
    if (child_canon == parent_canon) return false;
    return cfire::detail::is_same_or_ancestor_path(parent_canon, child_canon);
}

inline void clean_project_artifacts(
    const CleanOptions& opts,
    CleanReport& rep,
    std::filesystem::path& resolved_project_root_out
) {
    std::filesystem::path start = opts.target_path.empty() ? std::filesystem::path(".") : opts.target_path;
    std::error_code ec;
    auto start_st = std::filesystem::symlink_status(start, ec);
    if (opts.has_explicit_target && std::filesystem::is_symlink(start_st)) {
        rep.skipped.push_back({start.string(), "refused symlink project root"});
        rep.ok = false;
        return;
    }

    std::filesystem::path project_root;
    std::filesystem::path manifest_path;
    if (opts.has_explicit_target) {
        if (start.filename() == "build.cf" && std::filesystem::is_regular_file(start, ec)) {
            manifest_path = start;
            project_root = start.parent_path().empty() ? std::filesystem::path(".") : start.parent_path();
        } else {
            project_root = start;
            if (std::filesystem::exists(project_root / "build.cf", ec)) {
                manifest_path = project_root / "build.cf";
            }
        }
    } else {
        auto cur = std::filesystem::weakly_canonical(start, ec);
        if (ec || cur.empty()) cur = std::filesystem::absolute(start, ec);
        for (int depth = 0; depth < 8 && !cur.empty(); ++depth) {
            if (std::filesystem::exists(cur / "build.cf", ec)) {
                project_root = cur;
                manifest_path = cur / "build.cf";
                break;
            }
            if (cur == cur.parent_path()) break;
            cur = cur.parent_path();
        }
        if (project_root.empty()) {
            project_root = ".";
        }
    }
    resolved_project_root_out = project_root;

    if (cfire::detail::is_dangerous_cache_root(project_root) ||
        (!opts.runtime_dir.empty() &&
         (cfire::detail::is_same_or_ancestor_path(project_root, opts.runtime_dir) ||
          cfire::detail::is_same_or_ancestor_path(project_root, derive_install_root(opts.runtime_dir))))) {
        rep.skipped.push_back({project_root.string(), "refused unsafe project root (/, $HOME, ~/.cfire, or toolchain ancestor)"});
        rep.ok = false;
        return;
    }

    auto proj_canon = std::filesystem::weakly_canonical(project_root, ec);
    if (ec || proj_canon.empty()) proj_canon = std::filesystem::absolute(project_root, ec);

    std::set<std::filesystem::path> cleaned_set;
    auto record_and_remove = [&](const std::filesystem::path& p) {
        auto norm = p.lexically_normal();
        if (cleaned_set.contains(norm)) return;
        cleaned_set.insert(norm);

        std::uintmax_t sz = path_byte_size_no_follow(p);
        std::uintmax_t cnt = 0;
        std::uintmax_t bytes = 0;
        std::string err;
        if (!remove_no_follow(p, opts.dry_run, cnt, bytes, err)) {
            rep.skipped.push_back({p.string(), err.empty() ? "failed to remove path" : err});
            rep.ok = false;
            return;
        }
        rep.entries_removed += cnt;
        rep.bytes_freed += bytes;
        rep.removed.push_back({p.string(), sz});
    };

    // 1. Nested `.cfire/` artifact directories (no symlink traversal)
    if (std::filesystem::is_directory(project_root, ec)) {
        std::vector<std::filesystem::path> stack{project_root};
        while (!stack.empty()) {
            auto cur = std::move(stack.back());
            stack.pop_back();
            for (const auto& entry : std::filesystem::directory_iterator(cur, ec)) {
                if (ec) break;
                auto est = entry.symlink_status(ec);
                if (ec) continue;
                const std::string fname = entry.path().filename().string();
                if (fname == ".cfire") {
                    if (std::filesystem::is_symlink(est)) {
                        record_and_remove(entry.path());
                        continue;
                    }
                    if (!std::filesystem::is_directory(est)) {
                        rep.skipped.push_back({entry.path().string(), "refused non-directory .cfire entry"});
                        rep.ok = false;
                        continue;
                    }
                    std::string unexpected;
                    for (const auto& sub : std::filesystem::directory_iterator(entry.path(), ec)) {
                        if (ec) {
                            unexpected = "unreadable entry";
                            break;
                        }
                        const std::string sub_name = sub.path().filename().string();
                        if (sub_name != "gen" && sub_name != "obj" && sub_name != "cache") {
                            unexpected = sub_name;
                            break;
                        }
                    }
                    if (!unexpected.empty()) {
                        rep.skipped.push_back({
                            entry.path().string(),
                            std::format(".cfire/ holds unexpected entry '{}' outside gen/obj/cache", unexpected)
                        });
                        rep.ok = false;
                    } else {
                        record_and_remove(entry.path());
                    }
                    continue;
                }
                if (std::filesystem::is_symlink(est) || !std::filesystem::is_directory(est)) {
                    continue;
                }
                if (fname == ".git" || fname == ".tako" || fname == "node_modules" ||
                    fname == "vendor" || fname == "dist" || fname == "ejected_cpp") {
                    continue;
                }
                stack.push_back(entry.path());
            }
        }
    }

    // 2. Exact target outputs declared in `build.cf`
    if (!manifest_path.empty()) {
        auto parsed = parse_build_cf(manifest_path);
        if (parsed) {
            auto try_clean_output = [&](const std::filesystem::path& out_p) {
                std::error_code oec;
                auto ost = std::filesystem::symlink_status(out_p, oec);
                if (oec || !std::filesystem::exists(ost)) return;

                if (!is_strict_descendant(proj_canon, out_p)) {
                    rep.skipped.push_back({out_p.string(), "refused output path outside project root"});
                    rep.ok = false;
                    return;
                }
                if (std::filesystem::is_symlink(ost)) {
                    record_and_remove(out_p);
                    return;
                }
                if (std::filesystem::is_directory(ost)) {
                    auto ext = out_p.extension().string();
                    if (ext != ".app" && ext != ".dSYM") {
                        rep.skipped.push_back({out_p.string(), "refused removing directory matching target name"});
                        rep.ok = false;
                        return;
                    }
                    if (dir_contains_cf_sources_no_follow(out_p)) {
                        rep.skipped.push_back({out_p.string(), "refused bundle directory containing .cf source files"});
                        rep.ok = false;
                        return;
                    }
                    record_and_remove(out_p);
                    return;
                }
                record_and_remove(out_p);
            };

            for (const auto& exe : parsed->executables) {
                if (exe.name.empty()) continue;
                std::filesystem::path base_out = exe.out_dir.empty()
                    ? project_root
                    : (project_root / exe.out_dir);

                std::vector<std::filesystem::path> native_candidates;
                auto cand = base_out / exe.name;
                auto cst = std::filesystem::symlink_status(cand, ec);
                if (std::filesystem::is_directory(cst) && !std::filesystem::is_symlink(cst)) {
                    // Matches build_commands.cpp: when <out>/<name> is a source dir, binary sits inside it.
                    native_candidates.push_back(cand / exe.name);
                    native_candidates.push_back(cand / (exe.name + ".exe"));
                    native_candidates.push_back(cand / (exe.name + ".app"));
                    native_candidates.push_back(cand / (exe.name + ".dSYM"));
                } else {
                    native_candidates.push_back(cand);
                    native_candidates.push_back(base_out / (exe.name + ".exe"));
                }
                native_candidates.push_back(base_out / (exe.name + ".app"));
                native_candidates.push_back(base_out / (exe.name + ".dSYM"));
                native_candidates.push_back(base_out / (exe.name + ".html"));
                native_candidates.push_back(base_out / (exe.name + ".js"));
                native_candidates.push_back(base_out / (exe.name + ".wasm"));

                for (const auto& p : native_candidates) {
                    try_clean_output(p);
                }
            }
        }
    }
}

inline void sweep_owned_legacy_caches(const CleanOptions& opts, CleanReport& rep) {
    const char* env_override = std::getenv("CFIRE_CACHE_DIR");
    const char* force_sweep = std::getenv("CFIRE_CLEAN_SWEEP_LEGACY");
    if (env_override && *env_override && (!force_sweep || std::string_view(force_sweep) != "1")) {
        return;
    }

    auto record_legacy_remove = [&](const std::filesystem::path& p) {
        if (!is_owned_by_current_user(p)) return;
        std::uintmax_t sz = path_byte_size_no_follow(p);
        std::uintmax_t cnt = 0;
        std::uintmax_t bytes = 0;
        std::string err;
        if (!remove_no_follow(p, opts.dry_run, cnt, bytes, err)) {
            rep.skipped.push_back({p.string(), err.empty() ? "failed to remove legacy cache entry" : err});
            rep.ok = false;
            return;
        }
        if (cnt > 0) {
            rep.entries_removed += cnt;
            rep.bytes_freed += bytes;
            rep.removed.push_back({p.string(), sz});
        }
    };

    // 1. Legacy ~/.cfire/cache (never touching ~/.cfire/{runtime,bin,config.json})
    if (auto home = user_home_dir(); !home.empty()) {
        std::error_code ec;
        auto legacy_home_cache = home / ".cfire" / "cache";
        auto st = std::filesystem::symlink_status(legacy_home_cache, ec);
        if (!ec && std::filesystem::exists(st)) {
            if (std::filesystem::is_symlink(st)) {
                record_legacy_remove(legacy_home_cache);
            } else if (std::filesystem::is_directory(st) && is_owned_by_current_user(legacy_home_cache)) {
                bool had_foreign = false;
                std::vector<std::filesystem::path> children;
                for (const auto& child : std::filesystem::directory_iterator(legacy_home_cache, ec)) {
                    if (ec) {
                        had_foreign = true;
                        break;
                    }
                    children.push_back(child.path());
                }
                for (const auto& child : children) {
                    const std::string fn = child.filename().string();
                    if (fn == cfire::detail::kCacheMarkerName || cfire::detail::is_known_cache_entry(fn)) {
                        record_legacy_remove(child);
                    } else {
                        had_foreign = true;
                        rep.skipped.push_back({child.string(), "unexpected file inside legacy ~/.cfire/cache preserved"});
                        rep.ok = false;
                    }
                }
                if (!had_foreign && !opts.dry_run) {
                    std::filesystem::remove(legacy_home_cache, ec);
                }
            }
        }
    }

    // 2. Legacy <runtime_dir>/.cfire_cache (written before 597fd87)
    if (!opts.runtime_dir.empty()) {
        std::error_code ec;
        auto legacy_rt_cache = opts.runtime_dir / ".cfire_cache";
        auto st = std::filesystem::symlink_status(legacy_rt_cache, ec);
        if (!ec && std::filesystem::exists(st)) {
            record_legacy_remove(legacy_rt_cache);
        }
    }

    // 3. Legacy $TMP entries owned by the current user
    std::error_code tec;
    auto tmp_root = std::filesystem::temp_directory_path(tec);
    if (!tec && std::filesystem::is_directory(tmp_root, tec)) {
        std::vector<std::filesystem::path> legacy_tmp_paths;
        for (const auto& entry : std::filesystem::directory_iterator(tmp_root, tec)) {
            if (tec) break;
            const std::string fn = entry.path().filename().string();
            const bool is_legacy =
                fn == "cfire_pch_cache" ||
                fn == "cfire_doctor_probe.cpp" ||
                fn.starts_with("cfire_clang_ast_") ||
                fn.starts_with("cfire_ld_probe_") ||
                (fn.starts_with("cfire-") && (fn.ends_with("-pch") || fn.ends_with("-cache")));
            if (is_legacy) {
                legacy_tmp_paths.push_back(entry.path());
            }
        }
        for (const auto& lp : legacy_tmp_paths) {
            record_legacy_remove(lp);
        }
    }
}

inline void clean_user_cache(const CleanOptions& opts, const std::filesystem::path& project_root, CleanReport& rep) {
    auto cache_root = resolve_configured_cache_root();
    if (!cache_root.empty()) {
        std::error_code ec;
        auto st = std::filesystem::symlink_status(cache_root, ec);

        const bool unsafe_root =
            std::filesystem::is_symlink(st) ||
            cfire::detail::is_dangerous_cache_root(cache_root) ||
            cfire::detail::is_same_or_ancestor_path(cache_root, std::filesystem::current_path(ec)) ||
            (!project_root.empty() && cfire::detail::is_same_or_ancestor_path(cache_root, project_root)) ||
            (!opts.runtime_dir.empty() &&
             (cfire::detail::is_same_or_ancestor_path(cache_root, opts.runtime_dir) ||
              cfire::detail::is_same_or_ancestor_path(cache_root, derive_install_root(opts.runtime_dir))));

        if (unsafe_root) {
            rep.skipped.push_back({
                cache_root.string(),
                "refused dangerous cache root (symlink, /, $HOME, ~/.cfire, or project/toolchain ancestor)"
            });
            rep.ok = false;
            return;
        }

        if (std::filesystem::exists(st)) {
            if (!std::filesystem::is_directory(st)) {
                rep.skipped.push_back({cache_root.string(), "cache root is not a directory"});
                rep.ok = false;
                return;
            }

            std::vector<std::filesystem::path> children;
            for (const auto& entry : std::filesystem::directory_iterator(cache_root, ec)) {
                if (ec) {
                    rep.skipped.push_back({cache_root.string(), std::format("unreadable cache directory: {}", ec.message())});
                    rep.ok = false;
                    return;
                }
                children.push_back(entry.path());
            }

            if (!children.empty()) {
                auto marker = cache_root / cfire::detail::kCacheMarkerName;
                auto mst = std::filesystem::symlink_status(marker, ec);
                if (ec || !std::filesystem::exists(mst) || std::filesystem::is_symlink(mst) ||
                    !std::filesystem::is_regular_file(mst)) {
                    rep.skipped.push_back({
                        cache_root.string(),
                        "refused non-empty cache directory without .cfire-cache marker"
                    });
                    rep.ok = false;
                    return;
                }
            }

            if (!opts.dry_run) {
                ProbeCache::instance().clear_memory_and_disk();
            }

            bool has_foreign = false;
            std::filesystem::path marker_path;
            for (const auto& child : children) {
                const std::string fn = child.filename().string();
                if (fn == cfire::detail::kCacheMarkerName) {
                    marker_path = child;
                    continue;
                }
                if (!cfire::detail::is_known_cache_entry(fn)) {
                    has_foreign = true;
                    rep.skipped.push_back({child.string(), "unknown entry inside cache directory preserved"});
                    rep.ok = false;
                    continue;
                }
                std::uintmax_t sz = path_byte_size_no_follow(child);
                std::uintmax_t cnt = 0;
                std::uintmax_t bytes = 0;
                std::string err;
                if (!remove_no_follow(child, opts.dry_run, cnt, bytes, err)) {
                    rep.skipped.push_back({child.string(), err.empty() ? "failed to remove cache entry" : err});
                    rep.ok = false;
                    has_foreign = true;
                    continue;
                }
                rep.entries_removed += cnt;
                rep.bytes_freed += bytes;
                rep.removed.push_back({child.string(), sz});
            }

            if (!has_foreign) {
                if (!marker_path.empty()) {
                    std::uintmax_t msz = path_byte_size_no_follow(marker_path);
                    std::uintmax_t mcnt = 0;
                    std::uintmax_t mbytes = 0;
                    std::string merr;
                    if (!remove_no_follow(marker_path, opts.dry_run, mcnt, mbytes, merr)) {
                        rep.skipped.push_back({marker_path.string(), merr});
                        rep.ok = false;
                    } else {
                        rep.entries_removed += mcnt;
                        rep.bytes_freed += mbytes;
                        rep.removed.push_back({marker_path.string(), msz});
                    }
                }
                if (!opts.dry_run) {
                    std::filesystem::remove(cache_root, ec);
                }
                rep.entries_removed += 1;
            }
        }
    }

    sweep_owned_legacy_caches(opts, rep);
}

} // namespace clean_detail

[[nodiscard]] inline auto run_clean(const CleanOptions& opts) -> CleanReport {
    CleanReport rep;
    rep.dry_run = opts.dry_run;
    std::filesystem::path project_root;
    if (opts.clean_project) {
        clean_detail::clean_project_artifacts(opts, rep, project_root);
    }
    if (opts.clean_cache) {
        clean_detail::clean_user_cache(opts, project_root, rep);
    }
    return rep;
}

[[nodiscard]] inline auto format_clean_report_json(const CleanReport& rep) -> std::string {
    std::string out = std::format(
        "{{\"ok\": {}, \"dry_run\": {}, \"entries_removed\": {}, \"bytes_freed\": {}, \"removed\": [",
        rep.ok ? "true" : "false",
        rep.dry_run ? "true" : "false",
        rep.entries_removed,
        rep.bytes_freed
    );
    for (std::size_t i = 0; i < rep.removed.size(); ++i) {
        if (i > 0) out += ", ";
        out += std::format("{{\"path\": \"{}\", \"bytes\": {}}}",
            json_escape_str(rep.removed[i].path), rep.removed[i].bytes);
    }
    out += "], \"skipped\": [";
    for (std::size_t i = 0; i < rep.skipped.size(); ++i) {
        if (i > 0) out += ", ";
        out += std::format("{{\"path\": \"{}\", \"reason\": \"{}\"}}",
            json_escape_str(rep.skipped[i].path), json_escape_str(rep.skipped[i].reason));
    }
    out += "]}";
    return out;
}

} // namespace cfire::build
