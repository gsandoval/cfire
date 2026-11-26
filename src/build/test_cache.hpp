#pragma once
// src/build/test_cache.hpp — Transitive Test Dependency Graph, Incremental Change Detection & PASS Cache

#include "../buildsys.hpp"
#include "cache_paths.hpp"
#include "package_loader.hpp"
#include "pch.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cfire::build {

struct TestSuiteDeps {
    std::filesystem::path entry_file{};
    std::vector<std::filesystem::path> cf_files{};
    std::vector<std::filesystem::path> build_cfs{};
    std::vector<std::filesystem::path> cpp_headers{};
    std::unordered_map<std::string, std::string> file_hashes{};
};

[[nodiscard]] inline auto canonical_or_abs_path(const std::filesystem::path& p) -> std::filesystem::path {
    std::error_code ec;
    auto c = std::filesystem::weakly_canonical(p, ec);
    if (!ec && !c.empty()) return c;
    auto a = std::filesystem::absolute(p, ec);
    if (!ec && !a.empty()) return a.lexically_normal();
    return p.lexically_normal();
}

[[nodiscard]] inline auto trim_test_cache_ws(std::string_view s) -> std::string {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
        s.remove_suffix(1);
    }
    return std::string(s);
}

[[nodiscard]] inline auto collect_test_suite_deps(
    const std::filesystem::path& suite_file,
    const std::filesystem::path& runtime_dir,
    const std::string& target_triple,
    const std::string& sysroot,
    const std::vector<std::string>& extra_includes,
    const std::vector<std::string>& extra_pkg_configs,
    const std::vector<std::string>& extra_defines
) -> std::expected<TestSuiteDeps, std::string> {
    TestSuiteDeps deps;
    deps.entry_file = canonical_or_abs_path(suite_file);

    auto dir = suite_file.parent_path();
    if (dir.empty()) dir = ".";

    auto plan_res = init_build_plan_flags(
        suite_file, runtime_dir, /*test_mode=*/true, target_triple, sysroot, "auto", "auto", false, extra_pkg_configs, extra_defines);
    if (!plan_res) return std::unexpected(plan_res.error());
    BuildPlan plan = std::move(*plan_res);

    for (const auto& inc : extra_includes) {
        if (inc.empty()) continue;
        if (std::find(plan.extra_includes.begin(), plan.extra_includes.end(), inc) == plan.extra_includes.end()) {
            plan.extra_includes.push_back(inc);
        }
        std::string iflag = "-I" + inc;
        if (std::find(plan.cflags.begin(), plan.cflags.end(), iflag) == plan.cflags.end()) {
            plan.cflags.push_back(std::move(iflag));
        }
    }

    if (auto bcf_res = discover_and_apply_build_cf(plan, suite_file, dir, /*test_mode=*/true); !bcf_res) {
        return std::unexpected(bcf_res.error());
    }

    auto raw = read_file_text(suite_file);
    if (!raw) return std::unexpected(raw.error());
    auto ast = frontend::parse_source(suite_file.string(), *raw);
    if (!ast) return std::unexpected(ast.error());

    // Pass non-null empty overlay map so package_loader skips side-effectful pre_build_commands during cache probing.
    const std::unordered_map<std::string, std::string> dry_overlays;
    if (auto r = resolve_local_cf_imports(dir, *ast, plan, runtime_dir, /*test_mode=*/true, &dry_overlays); !r) {
        return std::unexpected(r.error());
    }

    auto add_unique_sorted = [](std::vector<std::filesystem::path>& vec, const std::filesystem::path& p) {
        auto c = canonical_or_abs_path(p);
        vec.push_back(std::move(c));
    };

    std::error_code ec;
    add_unique_sorted(deps.cf_files, suite_file);
    for (const auto& cf : plan.resolved_cf_files) {
        add_unique_sorted(deps.cf_files, cf);
    }
    for (const auto& bf : plan.resolved_build_cfs) {
        add_unique_sorted(deps.build_cfs, bf);
    }
    {
        // Include ancestor build.cf discovered by discover_and_apply_build_cf if present.
        auto cur = canonical_or_abs_path(dir);
        for (int depth = 0; depth < 8 && !cur.empty(); ++depth) {
            auto cand = cur / "build.cf";
            if (std::filesystem::is_regular_file(cand, ec) && !ec) {
                add_unique_sorted(deps.build_cfs, cand);
                break;
            }
            if (cur == cur.parent_path()) break;
            cur = cur.parent_path();
        }
    }

    // Collect local directories containing any imported .cf files so `import cpp "local.hpp"` resolves accurately.
    std::vector<std::filesystem::path> search_dirs;
    search_dirs.push_back(canonical_or_abs_path(dir));
    for (const auto& cf : deps.cf_files) {
        auto pd = cf.parent_path();
        if (!pd.empty() && std::find(search_dirs.begin(), search_dirs.end(), pd) == search_dirs.end()) {
            search_dirs.push_back(pd);
        }
    }
    for (const auto& inc_dir : plan.extra_includes) {
        if (!inc_dir.empty()) search_dirs.push_back(canonical_or_abs_path(inc_dir));
    }
    for (const auto& cf_flag : plan.cflags) {
        if (!cf_flag.starts_with("-I") || cf_flag.size() <= 2) continue;
        std::string inc_dir = dep_strip_quotes(cf_flag.substr(2));
        if (!inc_dir.empty()) search_dirs.push_back(canonical_or_abs_path(inc_dir));
    }

    auto resolve_local_hdr = [&](std::string_view hdr_key) -> std::optional<std::filesystem::path> {
        std::string clean(hdr_key);
        while (!clean.empty() && (clean.front() == '"' || clean.front() == '<')) clean.erase(clean.begin());
        while (!clean.empty() && (clean.back() == '"' || clean.back() == '>')) clean.pop_back();
        if (clean.empty()) return std::nullopt;
        std::error_code hec;
        std::filesystem::path hp(clean);
        if (hp.is_absolute() && std::filesystem::is_regular_file(hp, hec) && !hec) {
            return canonical_or_abs_path(hp);
        }
        hec.clear();
        for (const auto& sdir : search_dirs) {
            auto cand = sdir / clean;
            if (std::filesystem::is_regular_file(cand, hec) && !hec) {
                return canonical_or_abs_path(cand);
            }
            hec.clear();
        }
        return std::nullopt;
    };

    for (const auto& imp : ast->imports) {
        if (!imp.is_cpp) continue;
        if (auto rp = resolve_local_hdr(imp.path)) {
            add_unique_sorted(deps.cpp_headers, *rp);
        }
    }

    auto dedup_sort = [](std::vector<std::filesystem::path>& vec) {
        std::sort(vec.begin(), vec.end());
        vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
    };
    dedup_sort(deps.cf_files);
    dedup_sort(deps.build_cfs);
    dedup_sort(deps.cpp_headers);

    return deps;
}

[[nodiscard]] inline auto compute_test_cache_key(
    TestSuiteDeps& deps,
    const std::filesystem::path& runtime_dir,
    bool sanitize,
    const std::string& target_triple,
    const std::string& sysroot,
    const std::vector<std::string>& extra_link_libs,
    const std::vector<std::string>& extra_includes,
    const std::vector<std::string>& extra_pkg_configs,
    const std::vector<std::string>& extra_defines,
    const std::string& filter_pattern,
    bool json_mode
) -> std::string {
    const std::string compiler = resolve_cxx_compiler(target_triple);
    const std::string compiler_fp = compiler_binary_fingerprint(compiler);
    const std::string rt_fp = runtime_headers_fingerprint(runtime_dir);
    const bool dev_debug = is_dev_debug_profile();
    const std::string opt_flags = sanitize
        ? sanitizer_opt_flags()
        : (dev_debug ? "-O0 -g" : "-O2 -g");

    detail::Sha256State st;
    auto feed = [&](std::string_view tag, std::string_view val) {
        st.update(reinterpret_cast<const uint8_t*>(tag.data()), tag.size());
        const char sep1 = ':';
        st.update(reinterpret_cast<const uint8_t*>(&sep1), 1);
        st.update(reinterpret_cast<const uint8_t*>(val.data()), val.size());
        const char sep2 = '\0';
        st.update(reinterpret_cast<const uint8_t*>(&sep2), 1);
    };

    feed("schema", "test-cache-v1");
    feed("compiler", compiler_fp);
    feed("rt", rt_fp);
    feed("opt", opt_flags);
    feed("dev_debug", dev_debug ? "1" : "0");
    feed("sanitize", sanitize ? "1" : "0");
    feed("san_set", sanitize ? g_sanitizer_set : "");
    feed("no_trap", g_no_trap ? "1" : "0");
    feed("target", target_triple);
    feed("sysroot", sysroot);
    feed("filter", filter_pattern);
    feed("json", json_mode ? "1" : "0");
    for (const auto& v : extra_link_libs) feed("ll", v);
    for (const auto& v : extra_includes) feed("inc", v);
    for (const auto& v : extra_pkg_configs) feed("pc", v);
    for (const auto& v : extra_defines) feed("def", v);

    auto feed_files = [&](std::string_view tag, const std::vector<std::filesystem::path>& files) {
        for (const auto& f : files) {
            bool ok = false;
            std::string h = sha256_file_hex(f, &ok);
            if (!ok) h = "MISSING";
            std::string p_str = f.generic_string();
            deps.file_hashes[p_str] = h;
            feed(tag, p_str);
            feed("sha", h);
        }
    };
    feed_files("cf", deps.cf_files);
    feed_files("bcf", deps.build_cfs);
    feed_files("hdr", deps.cpp_headers);

    return st.final_hex();
}

class TestResultCache {
public:
    static constexpr uint64_t kMaxBytes = 64ULL * 1024ULL * 1024ULL; // 64 MiB

    [[nodiscard]] static auto dir() -> std::filesystem::path {
        return cache_subdir("test");
    }

    [[nodiscard]] static auto try_load(const std::string& key, std::string& out) -> bool {
        if (key.empty()) return false;
        auto p = dir() / (key + ".pass");
        std::error_code ec;
        if (!std::filesystem::is_regular_file(p, ec) || ec) return false;
        if (!read_text_file(p, out)) return false;
        std::filesystem::last_write_time(p, std::filesystem::file_time_type::clock::now(), ec);
        return true;
    }

    static void store_pass(const std::string& key, std::string_view out) {
        if (key.empty()) return;
        auto d = dir();
        auto p = d / (key + ".pass");
        if (!atomic_write_text_file(p, out)) return;
        evict_to_budget(kMaxBytes, key);
    }

    static void evict_to_budget(uint64_t max_bytes, const std::string& preserve_key = "") {
        auto d = dir();
        std::error_code ec;
        if (!std::filesystem::is_directory(d, ec) || ec) return;
        struct Entry {
            std::filesystem::path path;
            std::string stem;
            uint64_t size = 0;
            int64_t mtime_ns = 0;
        };
        std::vector<Entry> items;
        uint64_t total_size = 0;
        for (const auto& de : std::filesystem::directory_iterator(d, ec)) {
            if (ec) break;
            if (!de.is_regular_file(ec) || de.path().extension() != ".pass") continue;
            uint64_t sz = de.file_size(ec);
            if (ec) continue;
            auto ft = de.last_write_time(ec);
            int64_t ns = ec ? 0 : std::chrono::duration_cast<std::chrono::nanoseconds>(
                ft.time_since_epoch()).count();
            items.push_back({de.path(), de.path().stem().string(), sz, ns});
            total_size += sz;
        }
        if (total_size <= max_bytes && items.size() <= 2000) return;
        std::sort(items.begin(), items.end(), [](const Entry& a, const Entry& b) {
            return a.mtime_ns < b.mtime_ns;
        });
        std::size_t remaining = items.size();
        for (const auto& item : items) {
            if (total_size <= max_bytes && remaining <= 2000) break;
            if (!preserve_key.empty() && item.stem == preserve_key) continue;
            std::filesystem::remove(item.path, ec);
            total_size = (total_size >= item.size) ? (total_size - item.size) : 0;
            if (remaining > 0) --remaining;
        }
    }

    [[nodiscard]] static auto format_cached_output(std::string_view raw_out, bool json_mode) -> std::string {
        if (json_mode) {
            std::string s(raw_out);
            auto brace = s.find('{');
            if (brace != std::string::npos && s.find("\"cached\"") == std::string::npos) {
                s.insert(brace + 1, "\"cached\": true, ");
            }
            return s;
        }
        std::string s(raw_out);
        // Prefix the summary PASS line with `(cached) ` while keeping full substring parity.
        std::size_t search_from = 0;
        std::size_t last_pass_line = std::string::npos;
        while (search_from < s.size()) {
            std::size_t line_start = search_from;
            if (std::string_view(s).substr(line_start).starts_with("PASS (")) {
                last_pass_line = line_start;
            }
            std::size_t nl = s.find('\n', search_from);
            if (nl == std::string::npos) break;
            search_from = nl + 1;
        }
        if (last_pass_line != std::string::npos) {
            s.insert(last_pass_line, "(cached) ");
            return s;
        }
        if (!s.empty() && s.back() != '\n') s.push_back('\n');
        s += "(cached) PASS\n";
        return s;
    }
};

[[nodiscard]] inline auto is_hash_snapshot_dir_skipped(const std::filesystem::path& p) -> bool {
    std::string fn = p.filename().string();
    return fn == ".git" || fn == ".cfire" || fn == ".tako" ||
           fn == "node_modules" || fn == "vendor" || fn == "dist" || fn == "ejected_cpp";
}

[[nodiscard]] inline auto is_tracked_test_source_file(const std::filesystem::path& p) -> bool {
    std::string fn = p.filename().string();
    if (fn == "build.cf") return true;
    auto ext = p.extension().string();
    return ext == ".cf" || ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx";
}

[[nodiscard]] inline auto load_workspace_file_hashes(
    const std::filesystem::path& root_dir
) -> std::unordered_map<std::string, std::string> {
    std::unordered_map<std::string, std::string> hashes;
    auto path = canonical_or_abs_path(root_dir) / ".cfire" / "cache" / "test_file_hashes.txt";
    std::string raw;
    if (!read_text_file(path, raw)) return hashes;
    std::size_t pos = 0;
    while (pos < raw.size()) {
        std::size_t nl = raw.find('\n', pos);
        std::string_view line = (nl == std::string::npos)
            ? std::string_view(raw).substr(pos)
            : std::string_view(raw).substr(pos, nl - pos);
        pos = (nl == std::string::npos) ? raw.size() : nl + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        auto tab = line.find('\t');
        if (tab == std::string_view::npos || tab == 0 || tab + 1 >= line.size()) continue;
        hashes[std::string(line.substr(tab + 1))] = std::string(line.substr(0, tab));
    }
    return hashes;
}

inline void record_workspace_file_hashes(
    const std::filesystem::path& root_dir,
    const std::unordered_map<std::string, std::string>& new_hashes
) {
    if (new_hashes.empty()) return;
    auto canon_root = canonical_or_abs_path(root_dir);
    auto merged = load_workspace_file_hashes(canon_root);
    for (const auto& [path_s, sha] : new_hashes) {
        if (!path_s.empty() && !sha.empty()) {
            merged[path_s] = sha;
        }
    }
    std::vector<std::pair<std::string, std::string>> sorted(merged.begin(), merged.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
    std::string body;
    for (const auto& [p, h] : sorted) {
        body += h;
        body.push_back('\t');
        body += p;
        body.push_back('\n');
    }
    auto path = canon_root / ".cfire" / "cache" / "test_file_hashes.txt";
    (void)atomic_write_text_file(path, body);
}

[[nodiscard]] inline auto detect_changed_workspace_files(
    const std::filesystem::path& root_dir,
    std::vector<std::filesystem::path>& out_changed,
    bool& used_git
) -> bool {
    out_changed.clear();
    used_git = false;
    auto canon_root = canonical_or_abs_path(root_dir);

    int top_rc = 0;
    std::string top_out = trim_test_cache_ws(bridge::run_command_capture(
        std::format("git -C {} rev-parse --show-toplevel 2>/dev/null", bridge::shell_quote(canon_root.string())),
        top_rc));

    if (top_rc == 0 && !top_out.empty()) {
        auto repo_root = canonical_or_abs_path(top_out);
        int st_rc = 0;
        std::string st_out = bridge::run_command_capture(
            std::format("git -C {} status --porcelain=v1 -uall 2>/dev/null", bridge::shell_quote(repo_root.string())),
            st_rc);
        if (st_rc == 0) {
            used_git = true;
            auto unquote_git_path = [](std::string_view p) -> std::string {
                std::string s = trim_test_cache_ws(p);
                if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
                    s = s.substr(1, s.size() - 2);
                }
                return s;
            };
            std::size_t pos = 0;
            while (pos < st_out.size()) {
                std::size_t nl = st_out.find('\n', pos);
                std::string_view line = (nl == std::string::npos)
                    ? std::string_view(st_out).substr(pos)
                    : std::string_view(st_out).substr(pos, nl - pos);
                pos = (nl == std::string::npos) ? st_out.size() : nl + 1;
                if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
                if (line.size() < 4) continue;
                std::string_view rel_view = line.substr(3);
                if (auto arrow = rel_view.find(" -> "); arrow != std::string_view::npos) {
                    std::string p1 = unquote_git_path(rel_view.substr(0, arrow));
                    std::string p2 = unquote_git_path(rel_view.substr(arrow + 4));
                    if (!p1.empty()) out_changed.push_back(canonical_or_abs_path(repo_root / p1));
                    if (!p2.empty()) out_changed.push_back(canonical_or_abs_path(repo_root / p2));
                } else {
                    std::string p = unquote_git_path(rel_view);
                    if (!p.empty()) out_changed.push_back(canonical_or_abs_path(repo_root / p));
                }
            }
            return true;
        }
    }

    // Fallback for non-git workspaces: compare content hashes against .cfire/cache/test_file_hashes.txt.
    auto prev_hashes = load_workspace_file_hashes(canon_root);
    std::error_code ec;
    if (!std::filesystem::is_directory(canon_root, ec)) {
        return false;
    }
    std::vector<std::filesystem::path> stack{canon_root};
    while (!stack.empty()) {
        auto cur = std::move(stack.back());
        stack.pop_back();
        for (const auto& de : std::filesystem::directory_iterator(cur, ec)) {
            if (ec) break;
            auto st = de.symlink_status(ec);
            if (ec || std::filesystem::is_symlink(st)) continue;
            if (std::filesystem::is_directory(st)) {
                if (!is_hash_snapshot_dir_skipped(de.path())) {
                    stack.push_back(de.path());
                }
                continue;
            }
            if (!std::filesystem::is_regular_file(st) || !is_tracked_test_source_file(de.path())) {
                continue;
            }
            auto cp = canonical_or_abs_path(de.path());
            std::string cp_str = cp.generic_string();
            bool h_ok = false;
            std::string h = sha256_file_hex(cp, &h_ok);
            auto it = prev_hashes.find(cp_str);
            if (it == prev_hashes.end() || !h_ok || it->second != h) {
                out_changed.push_back(std::move(cp));
            }
        }
    }
    // Deleted tracked files also invalidate dependents.
    for (const auto& [old_p, _] : prev_hashes) {
        if (!std::filesystem::exists(std::filesystem::path(old_p), ec)) {
            out_changed.emplace_back(old_p);
        }
    }
    return true;
}

[[nodiscard]] inline auto suite_affected_by_changes(
    const TestSuiteDeps& deps,
    const std::unordered_set<std::string>& changed_canon_set
) -> bool {
    if (changed_canon_set.empty()) return false;
    auto any_in_set = [&](const std::vector<std::filesystem::path>& vec) {
        for (const auto& p : vec) {
            std::string g = canonical_or_abs_path(p).generic_string();
            if (changed_canon_set.contains(g)) return true;
        }
        return false;
    };
    return any_in_set(deps.cf_files) || any_in_set(deps.build_cfs) || any_in_set(deps.cpp_headers);
}

} // namespace cfire::build
