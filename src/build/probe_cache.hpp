#pragma once
// src/build/probe_cache.hpp — Persistent disk cache for compiler, linker, pkg-config & SDK probes

#include "cache_paths.hpp"
#include "toolchain_config.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace cfire::build {

struct CompilerProbeResult {
    bool ok = false;
    std::string dumpversion;
    int major_version = 0;
    std::string version_first_line;
    std::string auto_fast_ld_flag; // "-fuse-ld=mold", "-fuse-ld=lld", or ""
};

struct PkgConfigProbeResult {
    bool found = false;
    std::string cflags;
    std::string libs;
    std::string pc_path;
    std::string modversion;
    std::string error;
};

namespace detail {

inline bool run_probe_cmd_capture(const std::string& cmd, std::string& output, int& exit_code) {
    output.clear();
#if defined(_WIN32)
    FILE* fp = _popen(cmd.c_str(), "r");
#else
    FILE* fp = ::popen(cmd.c_str(), "r");
#endif
    if (!fp) {
        exit_code = -1;
        return false;
    }
    char buf[512];
    while (std::fgets(buf, sizeof(buf), fp) != nullptr) {
        output += buf;
    }
#if defined(_WIN32)
    exit_code = _pclose(fp);
#else
    int raw = ::pclose(fp);
    if (raw == -1) {
        exit_code = -1;
    } else if (WIFEXITED(raw)) {
        exit_code = WEXITSTATUS(raw);
    } else {
        exit_code = raw;
    }
#endif
    return true;
}

inline std::string trim_ws(std::string_view sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
        sv.remove_suffix(1);
    }
    return std::string(sv);
}

inline std::string quote_shell_arg_simple(std::string_view s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        return std::string(s);
    }
    // Wrap in double quotes escaping internal double quotes for simple probe arguments/paths.
    std::string out;
    out.reserve(s.size() + 4);
    out.push_back('"');
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else out.push_back(c);
    }
    out.push_back('"');
    return out;
}

inline std::string format_tool_invocation(std::string_view cmd) {
    if (cmd.empty()) return "";
    if (cmd.size() >= 2 && cmd.front() == '"' && cmd.back() == '"') {
        return std::string(cmd);
    }
    if (!probe_executable_candidate(std::filesystem::path(cmd)).empty()) {
        return quote_shell_arg_simple(cmd);
    }
    return std::string(cmd);
}

// Escape field separator '\x1f' via percent-encoding so flat record strings remain lossless.
inline std::string encode_record_fields(const std::vector<std::string_view>& fields) {
    std::string out;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) out.push_back('\x1f');
        for (char c : fields[i]) {
            if (c == '%') out += "%25";
            else if (c == '\x1f') out += "%1f";
            else if (c == '\n') out += "%0a";
            else if (c == '\r') out += "%0d";
            else out.push_back(c);
        }
    }
    return out;
}

inline std::string decode_record_field(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            std::string_view hex = s.substr(i + 1, 2);
            if (hex == "25") { out.push_back('%'); i += 2; continue; }
            if (hex == "1f") { out.push_back('\x1f'); i += 2; continue; }
            if (hex == "0a") { out.push_back('\n'); i += 2; continue; }
            if (hex == "0d") { out.push_back('\r'); i += 2; continue; }
        }
        out.push_back(s[i]);
    }
    return out;
}

inline std::vector<std::string> split_record_fields(std::string_view rec) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= rec.size()) {
        size_t sep = rec.find('\x1f', start);
        if (sep == std::string_view::npos) {
            out.push_back(decode_record_field(rec.substr(start)));
            break;
        }
        out.push_back(decode_record_field(rec.substr(start, sep - start)));
        start = sep + 1;
    }
    return out;
}

inline int parse_major_version_prefix(std::string_view dv) {
    int val = 0;
    bool any = false;
    for (char c : dv) {
        if (c >= '0' && c <= '9') {
            val = val * 10 + (c - '0');
            any = true;
        } else {
            break;
        }
    }
    return any ? val : 0;
}

inline std::vector<std::string> split_pc_search_paths(std::string_view sv) {
    std::vector<std::string> out;
#if defined(_WIN32)
    constexpr char kSep = ';';
#else
    constexpr char kSep = ':';
#endif
    size_t start = 0;
    while (start <= sv.size()) {
        size_t sep = sv.find(kSep, start);
        std::string_view part = (sep == std::string_view::npos) ? sv.substr(start) : sv.substr(start, sep - start);
        std::string trimmed = trim_ws(part);
        if (!trimmed.empty()) out.push_back(std::move(trimmed));
        if (sep == std::string_view::npos) break;
        start = sep + 1;
    }
    return out;
}

inline std::string null_stderr_redirect() {
#if defined(_WIN32)
    return " 2>NUL";
#else
    return " 2>/dev/null";
#endif
}

inline std::string null_all_redirect() {
#if defined(_WIN32)
    return " >NUL 2>&1";
#else
    return " >/dev/null 2>&1";
#endif
}

inline std::vector<std::string> query_pkg_config_pc_path_dirs(
    const std::string& pkg_bin,
    const char* env_pkg_path,
    const char* env_pkg_libdir
) {
    static std::mutex mu;
    static std::map<std::string, std::vector<std::string>> cache;
    std::string key = std::format("{}|{}|{}", pkg_bin, env_pkg_path ? env_pkg_path : "", env_pkg_libdir ? env_pkg_libdir : "");
    {
        std::lock_guard<std::mutex> lk(mu);
        if (auto it = cache.find(key); it != cache.end()) {
            return it->second;
        }
    }
    std::string out;
    int rc = -1;
    run_probe_cmd_capture(std::format("{} --variable pc_path pkg-config{}", pkg_bin, null_stderr_redirect()), out, rc);
    std::vector<std::string> dirs = (rc == 0) ? split_pc_search_paths(out) : std::vector<std::string>{};
    {
        std::lock_guard<std::mutex> lk(mu);
        cache[key] = dirs;
    }
    return dirs;
}

inline std::vector<std::string> parse_pc_requires_names(const std::filesystem::path& pc_file) {
    std::vector<std::string> deps;
    std::ifstream in(pc_file);
    if (!in.is_open()) return deps;
    std::string line;
    while (std::getline(in, line)) {
        std::string trimmed = trim_ws(line);
        std::string_view rest;
        if (trimmed.starts_with("Requires:")) {
            rest = std::string_view(trimmed).substr(9);
        } else if (trimmed.starts_with("Requires.private:")) {
            rest = std::string_view(trimmed).substr(17);
        } else {
            continue;
        }
        // Tokenize on whitespace and commas; skip relational operators and version tokens.
        std::vector<std::string> toks;
        std::string cur;
        auto flush_cur = [&]() {
            if (!cur.empty()) {
                toks.push_back(cur);
                cur.clear();
            }
        };
        for (char c : rest) {
            if (c == ',' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                flush_cur();
            } else if (c == '=' || c == '<' || c == '>') {
                flush_cur();
                toks.emplace_back(1, c);
            } else {
                cur.push_back(c);
            }
        }
        flush_cur();
        bool skip_next = false;
        for (const auto& tok : toks) {
            if (tok == "=" || tok == "<" || tok == ">" || tok == "!") {
                skip_next = true;
                continue;
            }
            if (skip_next) {
                skip_next = false;
                continue;
            }
            if (!tok.empty() && tok.find('$') == std::string::npos) {
                deps.push_back(tok);
            }
        }
    }
    return deps;
}

inline std::vector<std::string> collect_transitive_pc_files(
    const std::string& pkg_bin,
    const std::string& pc_args,
    const std::string& root_pc_path
) {
    std::vector<std::string> result;
    if (root_pc_path.empty()) return result;
    std::unordered_set<std::string> seen;
    std::queue<std::string> q;
    auto push_path = [&](const std::string& p) {
        if (p.empty()) return;
        std::error_code ec;
        std::filesystem::path fp(p);
        auto can = std::filesystem::weakly_canonical(fp, ec);
        std::string norm = (ec ? fp.lexically_normal() : can).string();
        if (seen.insert(norm).second) {
            result.push_back(p);
            q.push(p);
        }
    };
    push_path(root_pc_path);
    while (!q.empty()) {
        std::string cur_pc = std::move(q.front());
        q.pop();
        for (const auto& dep_pkg : parse_pc_requires_names(cur_pc)) {
            std::string dep_out;
            int rc = -1;
            std::string qdep = quote_shell_arg_simple(dep_pkg);
            run_probe_cmd_capture(
                std::format("{}{} --path {}{}", pkg_bin, pc_args, qdep, null_stderr_redirect()),
                dep_out, rc
            );
            if (rc == 0) {
                std::string dep_pc = trim_ws(dep_out);
                if (!dep_pc.empty()) {
                    push_path(dep_pc);
                }
            }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

inline std::string encode_pc_stamps(const std::vector<std::string>& pc_paths) {
    std::string out;
    for (const auto& p : pc_paths) {
        std::error_code ec;
        if (!std::filesystem::exists(p, ec) || ec) continue;
        if (!out.empty()) out.push_back('\n');
        out += p;
        out.push_back('\t');
        out += file_fingerprint_string(p);
    }
    return out;
}

inline bool verify_pc_stamps(std::string_view encoded) {
    if (encoded.empty()) return false;
    size_t start = 0;
    bool any = false;
    while (start <= encoded.size()) {
        size_t nl = encoded.find('\n', start);
        std::string_view line = (nl == std::string_view::npos) ? encoded.substr(start) : encoded.substr(start, nl - start);
        if (!line.empty()) {
            size_t tab = line.find('\t');
            if (tab == std::string_view::npos) return false;
            std::string_view p_sv = line.substr(0, tab);
            std::string_view stamp_sv = line.substr(tab + 1);
            std::filesystem::path p(p_sv);
            std::error_code ec;
            if (!std::filesystem::exists(p, ec) || ec) return false;
            if (file_fingerprint_string(p) != stamp_sv) return false;
            any = true;
        }
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
    return any;
}

inline std::string pkg_search_dir_stamps(
    const std::filesystem::path& install_root,
    const std::vector<std::string>& extra_pc_dirs,
    const char* env_pkg_path,
    const char* env_pkg_libdir,
    const std::string& pkg_bin = "pkg-config"
) {
    std::vector<std::filesystem::path> dirs;
    if (!install_root.empty()) {
        dirs.push_back(install_root / "lib" / "pkgconfig");
        dirs.push_back(install_root / "share" / "pkgconfig");
    }
    for (const auto& d : extra_pc_dirs) {
        if (!d.empty()) dirs.emplace_back(d);
    }
    for (const auto& d : split_pc_search_paths(env_pkg_path ? env_pkg_path : "")) {
        dirs.emplace_back(d);
    }
    for (const auto& d : split_pc_search_paths(env_pkg_libdir ? env_pkg_libdir : "")) {
        dirs.emplace_back(d);
    }
    for (const auto& d : query_pkg_config_pc_path_dirs(pkg_bin, env_pkg_path, env_pkg_libdir)) {
        dirs.emplace_back(d);
    }
#if !defined(_WIN32)
    dirs.emplace_back("/opt/homebrew/lib/pkgconfig");
    dirs.emplace_back("/usr/local/lib/pkgconfig");
    dirs.emplace_back("/usr/lib/pkgconfig");
#endif
    std::string stamp;
    for (const auto& d : dirs) {
        std::error_code ec;
        if (std::filesystem::exists(d, ec) && !ec) {
            stamp += file_fingerprint_string(d);
            stamp.push_back(';');
        }
    }
    return sha256_hex(stamp);
}

inline std::string probe_fast_linker_uncached(const std::string& cxx_cmd) {
#if defined(__APPLE__)
    (void)cxx_cmd;
    // Apple ld64 / ld-prime is already fast; do not override unless explicitly configured.
    return "";
#else
    if (cxx_cmd.empty()) return "";
    cfire::detail::SecureTempDir ld_tmp("cfire_ld_probe");
    if (!ld_tmp.ok()) return "";
    auto test_src = ld_tmp.file("probe.cpp");
    auto test_out = ld_tmp.file("probe.out");
    if (!atomic_write_text_file(test_src, "int main() { return 0; }\n")) {
        return "";
    }
    for (const char* flag : {"-fuse-ld=mold", "-fuse-ld=lld"}) {
        std::string out;
        int rc = -1;
        std::string cmd = std::format(
            "{} {} {} -o {}{}",
            format_tool_invocation(cxx_cmd),
            flag,
            quote_shell_arg_simple(test_src.string()),
            quote_shell_arg_simple(test_out.string()),
            null_all_redirect()
        );
        run_probe_cmd_capture(cmd, out, rc);
        std::error_code ec;
        std::filesystem::remove(test_out, ec);
        if (rc == 0) {
            return std::string(flag);
        }
    }
    return "";
#endif
}

} // namespace detail

class ProbeCache {
public:
    static ProbeCache& instance() {
        static ProbeCache cache;
        return cache;
    }

    std::filesystem::path file_path() const {
        return user_cache_root_dir() / "probes.json";
    }

    void clear_memory_and_disk() {
        std::lock_guard<std::mutex> lk(mu_);
        entries_.clear();
        loaded_path_.clear();
        loaded_stamp_.clear();
        std::error_code ec;
        std::filesystem::remove(file_path(), ec);
    }

    void set_bypass_cache(bool bypass) noexcept {
        bypass_cache_.store(bypass, std::memory_order_relaxed);
    }

    [[nodiscard]] bool bypass_cache() const noexcept {
        return bypass_cache_.load(std::memory_order_relaxed);
    }

    CompilerProbeResult probe_compiler(const std::string& cxx_cmd_in = "") {
        std::string cxx_cmd = cxx_cmd_in.empty() ? resolve_cxx_compiler() : cxx_cmd_in;
        if (cxx_cmd.empty()) return {};
        std::string fp = compiler_binary_fingerprint(cxx_cmd);
        std::string cache_key = "compiler|" + fp;
        const bool bypass = bypass_cache_.load(std::memory_order_relaxed);

        if (!bypass) {
            std::lock_guard<std::mutex> lk(mu_);
            ensure_loaded_locked();
            auto it = entries_.find(cache_key);
            if (it != entries_.end()) {
                auto fields = detail::split_record_fields(it->second);
                if (fields.size() >= 5) {
                    CompilerProbeResult res;
                    res.ok = (fields[0] == "1");
                    res.dumpversion = fields[1];
                    res.major_version = std::atoi(fields[2].c_str());
                    res.version_first_line = fields[3];
                    res.auto_fast_ld_flag = fields[4];
                    return res;
                }
            }
        }

        // Run probes outside lock
        CompilerProbeResult res;
        std::string qcxx = detail::format_tool_invocation(cxx_cmd);
        std::string ver_out;
        int ver_rc = -1;
        detail::run_probe_cmd_capture(qcxx + " --version 2>&1", ver_out, ver_rc);
        if (ver_rc == 0) {
            res.ok = true;
            size_t nl = ver_out.find('\n');
            res.version_first_line = detail::trim_ws(
                nl == std::string::npos ? std::string_view(ver_out) : std::string_view(ver_out).substr(0, nl)
            );
        }

        std::string dv_out;
        int dv_rc = -1;
        detail::run_probe_cmd_capture(qcxx + " -dumpversion" + detail::null_stderr_redirect(), dv_out, dv_rc);
        if (dv_rc == 0) {
            res.dumpversion = detail::trim_ws(dv_out);
            res.major_version = detail::parse_major_version_prefix(res.dumpversion);
        }
        if (res.ok) {
            res.auto_fast_ld_flag = detail::probe_fast_linker_uncached(cxx_cmd);
        }

        if (!bypass && res.ok) {
            std::lock_guard<std::mutex> lk(mu_);
            ensure_loaded_locked();
            entries_[cache_key] = detail::encode_record_fields({
                res.ok ? "1" : "0",
                res.dumpversion,
                std::to_string(res.major_version),
                res.version_first_line,
                res.auto_fast_ld_flag
            });
            persist_locked();
        }
        return res;
    }

    PkgConfigProbeResult probe_pkg_config(
        const std::string& pkg,
        bool static_link = false,
        const std::filesystem::path& install_root = {},
        const std::vector<std::string>& extra_pc_dirs = {}
    ) {
        if (pkg.empty()) return {};
        const char* env_pc = std::getenv("PKG_CONFIG");
        const char* env_pc_path = std::getenv("PKG_CONFIG_PATH");
        const char* env_pc_libdir = std::getenv("PKG_CONFIG_LIBDIR");
        const char* env_sysroot = std::getenv("PKG_CONFIG_SYSROOT_DIR");
        const char* env_path = std::getenv("PATH");
        std::string pkg_bin = (env_pc && *env_pc) ? detail::quote_shell_arg_simple(env_pc) : "pkg-config";
        std::string dir_stamp = detail::pkg_search_dir_stamps(
            install_root, extra_pc_dirs, env_pc_path, env_pc_libdir, pkg_bin
        );

        std::string extra_joined;
        for (const auto& d : extra_pc_dirs) {
            extra_joined += d;
            extra_joined.push_back('|');
        }
        std::string env_key = std::format(
            "{}|{}|{}|{}|{}|{}|{}|{}|{}",
            pkg,
            static_link ? "1" : "0",
            install_root.string(),
            extra_joined,
            env_pc ? env_pc : "",
            env_pc_path ? env_pc_path : "",
            env_pc_libdir ? env_pc_libdir : "",
            env_sysroot ? env_sysroot : "",
            env_path ? env_path : ""
        );
        std::string cache_key = "pkg2|" + sha256_hex(env_key);
        const bool bypass = bypass_cache_.load(std::memory_order_relaxed);

        if (!bypass) {
            std::lock_guard<std::mutex> lk(mu_);
            ensure_loaded_locked();
            auto it = entries_.find(cache_key);
            if (it != entries_.end()) {
                auto fields = detail::split_record_fields(it->second);
                if (fields.size() >= 7) {
                    bool found = (fields[0] == "1");
                    const std::string& cached_pc_path = fields[3];
                    const std::string& cached_pc_stamps = fields[4];
                    const std::string& cached_dir_stamp = fields[5];
                    bool valid = false;
                    // Never trust a negative cache entry; only positive hits with verified transitive `.pc` stamps apply.
                    if (found) {
                        if (!cached_pc_stamps.empty()) {
                            valid = detail::verify_pc_stamps(cached_pc_stamps);
                        } else if (cached_pc_path.empty() && cached_dir_stamp == dir_stamp) {
                            valid = true;
                        }
                    }
                    if (valid) {
                        return PkgConfigProbeResult{found, fields[1], fields[2], cached_pc_path, fields[6], ""};
                    }
                }
            }
        }

        // Cache miss or stale: invoke pkg-config using portable --with-path arguments.
        std::string pc_args;
        std::error_code dir_ec;
        if (!install_root.empty()) {
            for (const auto& pc_rel : {install_root / "lib" / "pkgconfig", install_root / "share" / "pkgconfig"}) {
                if (std::filesystem::is_directory(pc_rel, dir_ec)) {
                    pc_args += " --with-path=" + detail::quote_shell_arg_simple(pc_rel.string());
                }
            }
        }
        for (const auto& d : extra_pc_dirs) {
            if (!d.empty() && std::filesystem::is_directory(d, dir_ec)) {
                pc_args += " --with-path=" + detail::quote_shell_arg_simple(d);
            }
        }

        std::string qpkg = detail::quote_shell_arg_simple(pkg);
        std::string static_flag = static_link ? " --static" : "";

        PkgConfigProbeResult res;
        int exist_rc = -1;
        std::string exist_out;
        detail::run_probe_cmd_capture(
            std::format("{}{} --exists --print-errors {} 2>&1", pkg_bin, pc_args, qpkg),
            exist_out, exist_rc
        );
        if (exist_rc != 0) {
            res.found = false;
            res.error = detail::trim_ws(exist_out);
            if (!bypass) {
                std::lock_guard<std::mutex> lk(mu_);
                ensure_loaded_locked();
                if (entries_.erase(cache_key) > 0) {
                    persist_locked();
                }
            }
            return res;
        }

        std::string out_cf, out_lb, out_path, out_ver;
        int rc1 = -1, rc2 = -1, rc3 = -1, rc4 = -1;
        detail::run_probe_cmd_capture(
            std::format("{}{}{} --cflags --print-errors {} 2>&1", pkg_bin, pc_args, static_flag, qpkg),
            out_cf, rc1
        );
        detail::run_probe_cmd_capture(
            std::format("{}{}{} --libs --print-errors {} 2>&1", pkg_bin, pc_args, static_flag, qpkg),
            out_lb, rc2
        );
        if (rc1 != 0 || rc2 != 0) {
            res.found = false;
            res.error = detail::trim_ws(rc1 != 0 ? out_cf : out_lb);
            if (!bypass) {
                std::lock_guard<std::mutex> lk(mu_);
                ensure_loaded_locked();
                if (entries_.erase(cache_key) > 0) {
                    persist_locked();
                }
            }
            return res;
        }

        detail::run_probe_cmd_capture(
            std::format("{}{} --path {}{}", pkg_bin, pc_args, qpkg, detail::null_stderr_redirect()),
            out_path, rc3
        );
        detail::run_probe_cmd_capture(
            std::format("{}{} --modversion {}{}", pkg_bin, pc_args, qpkg, detail::null_stderr_redirect()),
            out_ver, rc4
        );
        res.found = true;
        res.cflags = detail::trim_ws(out_cf);
        res.libs = detail::trim_ws(out_lb);
        if (rc3 == 0) res.pc_path = detail::trim_ws(out_path);
        if (rc4 == 0) res.modversion = detail::trim_ws(out_ver);

        auto transitive_pcs = detail::collect_transitive_pc_files(pkg_bin, pc_args, res.pc_path);
        std::string pc_stamps = detail::encode_pc_stamps(transitive_pcs);
        if (!bypass) {
            std::lock_guard<std::mutex> lk(mu_);
            ensure_loaded_locked();
            entries_[cache_key] = detail::encode_record_fields({
                "1",
                res.cflags,
                res.libs,
                res.pc_path,
                pc_stamps,
                dir_stamp,
                res.modversion
            });
            persist_locked();
        }
        return res;
    }

    std::string probe_ios_sdk_path(const std::string& sdk_name) {
        if (sdk_name.empty()) return "";
        const char* dev_dir = std::getenv("DEVELOPER_DIR");
        std::string cache_key = "ios_sdk|" + sha256_hex(
            sdk_name + "|" + (dev_dir ? dev_dir : "")
        );
        const bool bypass = bypass_cache_.load(std::memory_order_relaxed);

        if (!bypass) {
            std::lock_guard<std::mutex> lk(mu_);
            ensure_loaded_locked();
            auto it = entries_.find(cache_key);
            if (it != entries_.end()) {
                auto fields = detail::split_record_fields(it->second);
                if (!fields.empty() && !fields[0].empty()) {
                    std::error_code ec;
                    if (std::filesystem::exists(fields[0], ec) && !ec) {
                        return fields[0];
                    }
                }
            }
        }

        std::string sdk_out;
        int rc = -1;
        detail::run_probe_cmd_capture(
            std::format("xcrun --sdk {} --show-sdk-path 2>/dev/null", sdk_name),
            sdk_out, rc
        );
        std::string resolved = (rc == 0) ? detail::trim_ws(sdk_out) : "";
        if (!bypass && !resolved.empty()) {
            std::lock_guard<std::mutex> lk(mu_);
            ensure_loaded_locked();
            entries_[cache_key] = detail::encode_record_fields({resolved});
            persist_locked();
        }
        return resolved;
    }

private:
    ProbeCache() = default;

    std::atomic<bool> bypass_cache_{false};

    void ensure_loaded_locked() {
        auto p = file_path();
        std::string p_str = p.string();
        std::string stamp;
        std::error_code ec;
        if (std::filesystem::exists(p, ec) && !ec) {
            stamp = file_fingerprint_string(p);
        }
        if (loaded_path_ == p_str && loaded_stamp_ == stamp) {
            return;
        }
        entries_.clear();
        loaded_path_ = p_str;
        loaded_stamp_ = stamp;
        std::string raw;
        if (read_text_file(p, raw) && !raw.empty()) {
            if (!parse_flat_string_map_json(raw, entries_)) {
                entries_.clear();
            }
        }
    }

    void persist_locked() {
        auto p = file_path();
        if (atomic_write_text_file(p, serialize_flat_string_map_json(entries_))) {
            loaded_path_ = p.string();
            std::error_code ec;
            if (std::filesystem::exists(p, ec) && !ec) {
                loaded_stamp_ = file_fingerprint_string(p);
            }
        }
    }

    std::mutex mu_;
    std::map<std::string, std::string> entries_;
    std::string loaded_path_;
    std::string loaded_stamp_;
};

inline std::string resolve_effective_linker_flag(const std::string& cxx_cmd = "") {
    std::string pref = resolve_linker_pref();
    if (!pref.empty()) {
        return format_linker_flag_from_pref(pref);
    }
    auto res = ProbeCache::instance().probe_compiler(cxx_cmd);
    return res.auto_fast_ld_flag;
}

} // namespace cfire::build
