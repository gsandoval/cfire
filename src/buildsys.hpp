#pragma once
// src/buildsys.hpp — C++23 Unified Build System, Live CC/CMake Builder & Multi-Package Resolver

#include "clang_bridge.hpp"
#include "codegen.hpp"
#include "parser.hpp"
#include "sema.hpp"
#include "cfire_core_stdlib.hpp"
#include <chrono>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cfire::build {

struct ExternalDepSpec {
    std::string name{};
    std::string driver{}; // "CMake", "CC", "Autotools", "Git", "Archive"
    std::string source{};
    std::string target{};
    std::vector<std::string> sources{};
    std::vector<std::string> includes{};
    std::unordered_map<std::string, std::string> defines{};
    std::string built_archive{}; // Path to compiled .a or shared (.so / .dylib) library
    bool is_shared{false};

    // Remote fetching & pinning fields (BS-EXT-01)
    std::string fetch_kind{}; // "git" | "archive"
    std::string url{};
    std::string ref{};       // branch, tag, or commit
    std::string sha256{};    // integrity hash for archives
    std::string policy{};    // "fetch" | "system" | "vendor" | "if-missing"
    bool is_remote{false};
};

struct LockedPackage {
    std::string name;
    std::string url;
    std::string ref;
    std::string commit;
    std::string sha256;
    std::string driver;
};

struct PreBuildCommandSpec {
    std::string command{};
    std::vector<std::string> args{};
    std::vector<std::string> inputs{};
    std::vector<std::string> outputs{};
    std::string work_dir{};
};

struct ParsedBuildCf {
    std::vector<ExternalDepSpec> external_deps{};
    std::vector<PreBuildCommandSpec> pre_build_commands{};
};

struct BuildPlan {
    std::string entry_file{};
    std::string generated_cpp{};
    std::vector<std::string> local_cf_packages{};
    std::vector<std::string> auto_linked{};
    std::vector<std::string> pkg_config_deps{};
    std::vector<std::string> cflags{};
    std::vector<std::string> ldflags{};
    std::unordered_map<std::string, bridge::HeaderIndex> cpp_headers{};
    std::vector<ExternalDepSpec> external_deps{};
    std::vector<PreBuildCommandSpec> pre_build_commands{};
    bool used_pch = false;
    bool sanitized = false;
    std::string target_triple{};
    std::string sysroot{};
    std::vector<std::string> extra_link_libs{};
    std::vector<std::string> extra_includes{};
};

[[nodiscard]] inline auto read_file_text(const std::filesystem::path& p) -> std::expected<std::string, std::string> {
    std::ifstream f(p, std::ios::in | std::ios::binary);
    if (!f.is_open()) {
        return std::unexpected(std::format("failed to open file: {}", p.string()));
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

[[nodiscard]] inline auto split_ws(std::string_view sv) -> std::vector<std::string> {
    std::vector<std::string> out;
    std::istringstream iss{std::string(sv)};
    std::string tok;
    while (iss >> tok) out.push_back(std::move(tok));
    return out;
}

inline void query_pkg_config(const std::string& pkg, std::vector<std::string>& cflags, std::vector<std::string>& libs) {
    int code = 0;
    std::string qpkg = bridge::shell_quote(pkg);
    std::string cf_out = bridge::run_command_capture("pkg-config --cflags " + qpkg, code);
    if (code == 0) {
        for (auto& f : split_ws(cf_out)) cflags.push_back(std::move(f));
    }
    std::string lb_out = bridge::run_command_capture("pkg-config --libs " + qpkg, code);
    if (code == 0) {
        for (auto& l : split_ws(lb_out)) libs.push_back(std::move(l));
    }
}

[[nodiscard]] inline auto parse_build_cf(const std::filesystem::path& build_cf_path)
    -> std::expected<ParsedBuildCf, std::string>
{
    auto raw = read_file_text(build_cf_path);
    if (!raw) return std::unexpected(raw.error());
    auto ast = frontend::parse_source(build_cf_path.string(), *raw);
    if (!ast) return std::unexpected(ast.error());

    ParsedBuildCf parsed;
    for (const auto& gv : ast->global_vars) {
        if (!gv.value) continue;
        const auto* call = std::get_if<frontend::CallExpr>(&gv.value->node);
        if (!call || !call->callee) continue;
        const auto* mem = std::get_if<frontend::MemberExpr>(&call->callee->node);
        if (!mem || mem->field == "Executable") continue;

        if (mem->field == "Command") {
            PreBuildCommandSpec cmd_spec;
            if (!call->args.empty() && call->args[0].value) {
                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&call->args[0].value->node)) {
                    cmd_spec.command = lit->value;
                }
            }
            if (call->args.size() > 1 && call->args[1].value) {
                if (const auto* st_lit = std::get_if<frontend::StructLitExpr>(&call->args[1].value->node)) {
                    for (const auto& fld : st_lit->fields) {
                        if (!fld.value) continue;
                        if (fld.name == "Args") {
                            if (const auto* arr = std::get_if<frontend::ArrayLitExpr>(&fld.value->node)) {
                                for (const auto& el : arr->elements) {
                                    if (const auto* lit = std::get_if<frontend::LiteralExpr>(&el->node)) {
                                        cmd_spec.args.push_back(lit->value);
                                    }
                                }
                            }
                        } else if (fld.name == "Inputs") {
                            if (const auto* arr = std::get_if<frontend::ArrayLitExpr>(&fld.value->node)) {
                                for (const auto& el : arr->elements) {
                                    if (const auto* lit = std::get_if<frontend::LiteralExpr>(&el->node)) {
                                        cmd_spec.inputs.push_back(lit->value);
                                    }
                                }
                            }
                        } else if (fld.name == "Outputs") {
                            if (const auto* arr = std::get_if<frontend::ArrayLitExpr>(&fld.value->node)) {
                                for (const auto& el : arr->elements) {
                                    if (const auto* lit = std::get_if<frontend::LiteralExpr>(&el->node)) {
                                        cmd_spec.outputs.push_back(lit->value);
                                    }
                                }
                            }
                        } else if (fld.name == "WorkDir") {
                            if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                                cmd_spec.work_dir = lit->value;
                            }
                        }
                    }
                }
            }
            parsed.pre_build_commands.push_back(std::move(cmd_spec));
            continue;
        }

        ExternalDepSpec spec;
        spec.name = gv.name;
        spec.driver = mem->field;
        if (mem->field == "Git") {
            spec.fetch_kind = "git";
            spec.is_remote = true;
            spec.driver = "CC";
        } else if (mem->field == "Archive") {
            spec.fetch_kind = "archive";
            spec.is_remote = true;
            spec.driver = "CC";
        }
        if (!call->args.empty() && call->args[0].value) {
            if (const auto* lit = std::get_if<frontend::LiteralExpr>(&call->args[0].value->node)) {
                spec.source = lit->value;
            }
        }
        if (call->args.size() > 1 && call->args[1].value) {
            if (const auto* st_lit = std::get_if<frontend::StructLitExpr>(&call->args[1].value->node)) {
                for (const auto& fld : st_lit->fields) {
                    if (!fld.value) continue;
                    if (fld.name == "Target") {
                        if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                            spec.target = lit->value;
                        }
                    } else if (fld.name == "Sources") {
                        if (const auto* arr = std::get_if<frontend::ArrayLitExpr>(&fld.value->node)) {
                            for (const auto& el : arr->elements) {
                                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&el->node)) {
                                    spec.sources.push_back(lit->value);
                                }
                            }
                        }
                    } else if (fld.name == "Includes") {
                        if (const auto* arr = std::get_if<frontend::ArrayLitExpr>(&fld.value->node)) {
                            for (const auto& el : arr->elements) {
                                if (const auto* lit = std::get_if<frontend::LiteralExpr>(&el->node)) {
                                    spec.includes.push_back(lit->value);
                                }
                            }
                        }
                    } else if (fld.name == "Defines" || fld.name == "Flags") {
                        if (const auto* map_lit = std::get_if<frontend::StructLitExpr>(&fld.value->node)) {
                            for (const auto& mf : map_lit->fields) {
                                if (mf.value) {
                                    if (const auto* lit = std::get_if<frontend::LiteralExpr>(&mf.value->node)) {
                                        spec.defines[mf.name] = lit->value;
                                    }
                                }
                            }
                        }
                    } else if (fld.name == "Url" || fld.name == "URL") {
                        if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                            spec.url = lit->value;
                            spec.is_remote = true;
                        }
                    } else if (fld.name == "Tag" || fld.name == "Ref" || fld.name == "Commit" || fld.name == "Branch") {
                        if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                            spec.ref = lit->value;
                        }
                    } else if (fld.name == "Sha256" || fld.name == "Hash" || fld.name == "Checksum") {
                        if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                            spec.sha256 = lit->value;
                        }
                    } else if (fld.name == "Policy") {
                        if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                            spec.policy = lit->value;
                        }
                    } else if (fld.name == "Driver") {
                        if (const auto* lit = std::get_if<frontend::LiteralExpr>(&fld.value->node)) {
                            spec.driver = lit->value;
                        }
                    }
                }
            }
        }
        if (spec.fetch_kind == "git" || spec.fetch_kind == "archive" ||
            spec.driver == "Git" || spec.driver == "Archive" ||
            spec.source.starts_with("https://") || spec.source.starts_with("http://") ||
            spec.source.ends_with(".git") || spec.source.starts_with("git@") ||
            !spec.url.empty()) {
            spec.is_remote = true;
            if (spec.url.empty()) spec.url = spec.source;
            if (spec.fetch_kind.empty()) {
                if (spec.driver == "Git" || spec.url.ends_with(".git") || spec.url.starts_with("git@")) {
                    spec.fetch_kind = "git";
                    if (spec.driver == "Git") spec.driver = "CC";
                } else if (spec.driver == "Archive" || spec.url.ends_with(".zip") || spec.url.ends_with(".tar.gz") || spec.url.ends_with(".tgz") || spec.url.ends_with(".tar.xz")) {
                    spec.fetch_kind = "archive";
                    if (spec.driver == "Archive") spec.driver = "CC";
                }
            }
        }
        parsed.external_deps.push_back(std::move(spec));
    }
    return parsed;
}

// Extracts tokens inside a CMake command like `add_library(kvstore STATIC src/kvstore.c)`
inline void parse_cmake_manifest(
    const std::filesystem::path& cmake_lists,
    ExternalDepSpec& dep
) {
    auto txt = read_file_text(cmake_lists);
    if (!txt) return;
    std::string content = *txt;

    auto extract_cmd_args = [&](std::string_view cmd_name) -> std::vector<std::string> {
        std::vector<std::string> args;
        std::size_t pos = content.find(cmd_name);
        if (pos == std::string::npos) return args;
        auto lp = content.find('(', pos + cmd_name.size());
        auto rp = content.find(')', lp != std::string::npos ? lp : 0);
        if (lp == std::string::npos || rp == std::string::npos || rp <= lp) return args;
        std::string inner = content.substr(lp + 1, rp - lp - 1);
        std::istringstream iss(inner);
        std::string tok;
        while (iss >> tok) {
            if (!tok.empty() && tok.front() == '"' && tok.back() == '"' && tok.size() >= 2) {
                tok = tok.substr(1, tok.size() - 2);
            }
            args.push_back(std::move(tok));
        }
        return args;
    };

    if (dep.sources.empty()) {
        auto lib_args = extract_cmd_args("add_library");
        for (std::size_t i = 1; i < lib_args.size(); ++i) {
            const auto& a = lib_args[i];
            if (a == "STATIC" || a == "SHARED" || a == "OBJECT" || a == "INTERFACE") continue;
            dep.sources.push_back(a);
        }
    }
    if (dep.includes.empty()) {
        auto inc_args = extract_cmd_args("target_include_directories");
        for (std::size_t i = 1; i < inc_args.size(); ++i) {
            const auto& a = inc_args[i];
            if (a == "PUBLIC" || a == "PRIVATE" || a == "INTERFACE") continue;
            dep.includes.push_back(a);
        }
    }
    auto def_args = extract_cmd_args("target_compile_definitions");
    for (std::size_t i = 1; i < def_args.size(); ++i) {
        std::string a = def_args[i];
        if (a == "PUBLIC" || a == "PRIVATE" || a == "INTERFACE") continue;
        if (a.starts_with("-D")) a = a.substr(2);
        if (auto eq = a.find('='); eq != std::string::npos) {
            dep.defines.try_emplace(a.substr(0, eq), a.substr(eq + 1));
        } else {
            dep.defines.try_emplace(a, "1");
        }
    }
}

inline auto trim_whitespace(std::string_view s) -> std::string {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return std::string(s);
}

inline auto get_package_cache_root() -> std::filesystem::path {
    if (const char* env = std::getenv("CFIRE_CACHE_DIR")) {
        auto p = std::filesystem::path(env) / "pkg";
        std::error_code ec;
        std::filesystem::create_directories(p, ec);
        return p;
    }
    if (const char* home = std::getenv("HOME")) {
        auto home_pkg = std::filesystem::path(home) / ".cfire" / "cache" / "pkg";
        std::error_code ec;
        std::filesystem::create_directories(home_pkg, ec);
        if (!ec) {
            auto probe = home_pkg / ".write_probe";
            std::ofstream probe_file(probe);
            if (probe_file.good()) {
                probe_file.close();
                std::filesystem::remove(probe, ec);
                return home_pkg;
            }
        }
    }
    auto tmp_pkg = std::filesystem::temp_directory_path() / "cfire_cache" / "pkg";
    std::error_code ec;
    std::filesystem::create_directories(tmp_pkg, ec);
    return tmp_pkg;
}

inline auto read_build_lock(const std::filesystem::path& lock_file) -> std::unordered_map<std::string, LockedPackage> {
    std::unordered_map<std::string, LockedPackage> out;
    auto txt = read_file_text(lock_file);
    if (!txt) return out;
    std::string content = *txt;

    std::size_t pos = content.find("\"packages\"");
    if (pos == std::string::npos) return out;

    std::size_t cur = pos;
    while ((cur = content.find('"', cur + 1)) != std::string::npos) {
        auto end_q = content.find('"', cur + 1);
        if (end_q == std::string::npos) break;
        std::string key = content.substr(cur + 1, end_q - cur - 1);
        cur = end_q + 1;
        while (cur < content.size() && (content[cur] == ' ' || content[cur] == ':' || content[cur] == '\n' || content[cur] == '\r')) ++cur;
        if (cur < content.size() && content[cur] == '{') {
            auto end_brace = content.find('}', cur);
            if (end_brace == std::string::npos) break;
            std::string block = content.substr(cur, end_brace - cur + 1);
            cur = end_brace + 1;

            auto extract_field = [&](std::string_view fld) -> std::string {
                std::string target = std::format("\"{}\"", fld);
                auto fp = block.find(target);
                if (fp == std::string::npos) return "";
                auto colon = block.find(':', fp);
                if (colon == std::string::npos) return "";
                auto q1 = block.find('"', colon);
                if (q1 == std::string::npos) return "";
                auto q2 = block.find('"', q1 + 1);
                if (q2 == std::string::npos) return "";
                return block.substr(q1 + 1, q2 - q1 - 1);
            };

            LockedPackage pkg;
            pkg.name = key;
            pkg.url = extract_field("url");
            pkg.ref = extract_field("ref");
            pkg.commit = extract_field("commit");
            pkg.sha256 = extract_field("sha256");
            pkg.driver = extract_field("driver");
            out[key] = std::move(pkg);
        }
    }
    return out;
}

inline auto write_build_lock(
    const std::filesystem::path& lock_file,
    const std::unordered_map<std::string, LockedPackage>& pkgs
) -> bool {
    std::string json = "{\n  \"version\": 1,\n  \"packages\": {\n";
    bool first = true;
    for (const auto& [name, pkg] : pkgs) {
        if (!first) json += ",\n";
        first = false;
        json += std::format("    \"{}\": {{\n", name);
        json += std::format("      \"url\": \"{}\",\n", pkg.url);
        if (!pkg.ref.empty()) json += std::format("      \"ref\": \"{}\",\n", pkg.ref);
        if (!pkg.commit.empty()) json += std::format("      \"commit\": \"{}\",\n", pkg.commit);
        if (!pkg.sha256.empty()) json += std::format("      \"sha256\": \"{}\",\n", pkg.sha256);
        json += std::format("      \"driver\": \"{}\"\n", pkg.driver);
        json += "    }";
    }
    json += "\n  }\n}\n";

    std::ofstream out(lock_file, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << json;
    return true;
}

inline auto fetch_git_repository(
    const std::string& url,
    const std::string& ref,
    const std::filesystem::path& dest_dir,
    std::string& resolved_commit
) -> std::expected<void, std::string> {
    std::error_code ec;
    std::filesystem::create_directories(dest_dir.parent_path(), ec);

    if (std::filesystem::exists(dest_dir / ".git", ec)) {
        int rc = 0;
        std::string rev = bridge::run_command_capture(
            std::format("git -C \"{}\" rev-parse HEAD", dest_dir.string()), rc);
        if (rc == 0) {
            resolved_commit = trim_whitespace(rev);
            return {};
        }
    }

    std::filesystem::remove_all(dest_dir, ec);

    std::string clone_cmd;
    if (!ref.empty()) {
        clone_cmd = std::format("git clone --depth 1 --branch \"{}\" \"{}\" \"{}\"", ref, url, dest_dir.string());
    } else {
        clone_cmd = std::format("git clone --depth 1 \"{}\" \"{}\"", url, dest_dir.string());
    }

    int rc = 0;
    std::string out = bridge::run_command_capture(clone_cmd, rc);
    if (rc != 0) {
        std::string fallback_cmd = std::format("git clone \"{}\" \"{}\"", url, dest_dir.string());
        out = bridge::run_command_capture(fallback_cmd, rc);
        if (rc != 0) {
            return std::unexpected(std::format("failed to clone git repository '{}':\n{}", url, out));
        }
        if (!ref.empty()) {
            std::string co_cmd = std::format("git -C \"{}\" checkout \"{}\"", dest_dir.string(), ref);
            out = bridge::run_command_capture(co_cmd, rc);
            if (rc != 0) {
                return std::unexpected(std::format("failed to checkout ref '{}' in '{}':\n{}", ref, url, out));
            }
        }
    }

    int rev_rc = 0;
    std::string rev_out = bridge::run_command_capture(
        std::format("git -C \"{}\" rev-parse HEAD", dest_dir.string()), rev_rc);
    if (rev_rc == 0) {
        resolved_commit = trim_whitespace(rev_out);
    }
    return {};
}

inline auto fetch_archive(
    const std::string& url,
    const std::string& expected_sha256,
    const std::filesystem::path& dest_dir,
    std::string& actual_sha256
) -> std::expected<void, std::string> {
    std::error_code ec;
    std::filesystem::create_directories(dest_dir.parent_path(), ec);

    if (std::filesystem::exists(dest_dir, ec) && !std::filesystem::is_empty(dest_dir, ec)) {
        return {};
    }

    auto now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
    auto tmp_archive = std::filesystem::temp_directory_path() / std::format("cfire_pkg_dl_{}_{}.archive", ::getpid(), now_ns);
    struct ArchiveCleanup {
        std::filesystem::path p;
        ~ArchiveCleanup() { std::error_code ec; std::filesystem::remove(p, ec); }
    } cleanup{tmp_archive};

    int rc = 0;
    std::string dl_cmd = std::format("curl -fsSL \"{}\" -o \"{}\"", url, tmp_archive.string());
    std::string out = bridge::run_command_capture(dl_cmd, rc);
    if (rc != 0) {
        return std::unexpected(std::format("failed to download archive from '{}':\n{}", url, out));
    }

    auto hash_res = cfire::sha256::hash_file_hex(tmp_archive);
    if (!hash_res) {
        return std::unexpected(hash_res.error());
    }
    actual_sha256 = *hash_res;

    auto to_lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };

    if (!expected_sha256.empty() && !actual_sha256.empty() && to_lower(actual_sha256) != to_lower(expected_sha256)) {
        return std::unexpected(std::format("cfire/INTEGRITY-MISMATCH: checksum mismatch for '{}': expected {}, got {}",
            url, expected_sha256, actual_sha256));
    }

    std::filesystem::create_directories(dest_dir, ec);

    int ext_rc = 0;
    if (url.ends_with(".zip")) {
        std::string unz_cmd = std::format("unzip -q -o \"{}\" -d \"{}\"", tmp_archive.string(), dest_dir.string());
        out = bridge::run_command_capture(unz_cmd, ext_rc);
    } else {
        std::string tar_cmd = std::format("tar -xf \"{}\" -C \"{}\" --strip-components=1 2>/dev/null || tar -xf \"{}\" -C \"{}\"",
            tmp_archive.string(), dest_dir.string(), tmp_archive.string(), dest_dir.string());
        out = bridge::run_command_capture(tar_cmd, ext_rc);
    }
    if (ext_rc != 0) {
        return std::unexpected(std::format("failed to extract archive from '{}':\n{}", url, out));
    }

    return {};
}

inline auto resolve_external_dep_source(
    const std::filesystem::path& project_dir,
    ExternalDepSpec& dep
) -> std::expected<std::filesystem::path, std::string> {
    auto vendor_dir = project_dir / "vendor" / dep.name;
    if (std::filesystem::exists(vendor_dir) && std::filesystem::is_directory(vendor_dir)) {
        return vendor_dir;
    }

    if (!dep.is_remote && !dep.source.empty()) {
        auto local_p = std::filesystem::absolute(project_dir / dep.source);
        if (std::filesystem::exists(local_p) && std::filesystem::is_directory(local_p)) {
            return local_p;
        }
    }

    if (dep.is_remote || !dep.url.empty()) {
        std::string target_url = dep.url.empty() ? dep.source : dep.url;
        std::string key = target_url + "@" + dep.ref;
        std::size_t h = std::hash<std::string>{}(key);
        auto cache_dest = get_package_cache_root() / std::format("{}_{:08x}", dep.name, static_cast<std::uint32_t>(h & 0xFFFFFFFF));

        std::filesystem::path lock_file = project_dir / "build.lock";
        auto lock_entries = read_build_lock(lock_file);

        if (auto it = lock_entries.find(dep.name); it != lock_entries.end()) {
            if (!it->second.commit.empty() && dep.ref.empty()) {
                dep.ref = it->second.commit;
            }
            if (!it->second.sha256.empty() && dep.sha256.empty()) {
                dep.sha256 = it->second.sha256;
            }
        }

        std::string resolved_commit;
        std::string actual_sha256;

        if (dep.fetch_kind == "git" || dep.driver == "Git" || target_url.ends_with(".git") || !dep.ref.empty() || target_url.starts_with("git@")) {
            auto fetch_res = fetch_git_repository(target_url, dep.ref, cache_dest, resolved_commit);
            if (!fetch_res) return std::unexpected(fetch_res.error());
        } else {
            auto fetch_res = fetch_archive(target_url, dep.sha256, cache_dest, actual_sha256);
            if (!fetch_res) return std::unexpected(fetch_res.error());
        }

        lock_entries[dep.name] = LockedPackage{
            .name = dep.name,
            .url = target_url,
            .ref = dep.ref,
            .commit = resolved_commit,
            .sha256 = !dep.sha256.empty() ? dep.sha256 : actual_sha256,
            .driver = dep.driver
        };
        (void)write_build_lock(lock_file, lock_entries);

        return cache_dest;
    }

    return std::filesystem::absolute(project_dir / dep.source);
}

// Compiles a local C/C++ dependency (`build.CC`, `build.CMake`, `build.Make`, or `build.Ninja`)
// into `.cfire/cache/lib/lib<name>.a` and appends its include paths and `.a` archive to `plan`.
[[nodiscard]] inline auto build_local_dep(
    const std::filesystem::path& project_dir,
    ExternalDepSpec& dep,
    BuildPlan& plan
) -> std::expected<void, std::string> {
    auto resolved_src = resolve_external_dep_source(project_dir, dep);
    if (!resolved_src) return std::unexpected(resolved_src.error());
    std::filesystem::path src_dir = *resolved_src;
    if (!std::filesystem::exists(src_dir) || !std::filesystem::is_directory(src_dir)) {
        // Remote reference without local checkout (metadata-only mode)
        return {};
    }

    // If CMakeLists.txt exists, enrich `dep` with any sources/includes/defines declared in CMakeLists.txt
    if (std::filesystem::exists(src_dir / "CMakeLists.txt")) {
        parse_cmake_manifest(src_dir / "CMakeLists.txt", dep);
    }

    // 1. Register public include directories (auto-discovering `include/` if present)
    if (dep.includes.empty()) {
        plan.cflags.push_back("-I" + src_dir.string());
        if (std::filesystem::is_directory(src_dir / "include")) {
            plan.cflags.push_back("-I" + (src_dir / "include").string());
        }
    } else {
        for (const auto& inc : dep.includes) {
            auto inc_path = (inc == ".") ? src_dir : (src_dir / inc);
            plan.cflags.push_back("-I" + inc_path.string());
        }
    }

    auto cache_root = std::filesystem::absolute(project_dir / ".cfire" / "cache");
    auto obj_dir = cache_root / "objs" / dep.name;
    auto lib_dir = cache_root / "lib";
    std::filesystem::create_directories(obj_dir);
    std::filesystem::create_directories(lib_dir);

    auto is_lib_file = [](const std::filesystem::path& p) -> bool {
        auto ext = p.extension().string();
        if (ext == ".a" || ext == ".dylib" || ext == ".so") return true;
        std::string fn = p.filename().string();
        return fn.find(".so.") != std::string::npos;
    };
    auto is_shared_lib = [](const std::filesystem::path& p) -> bool {
        auto ext = p.extension().string();
        if (ext == ".dylib" || ext == ".so") return true;
        std::string fn = p.filename().string();
        return fn.find(".so.") != std::string::npos;
    };
    auto register_lib = [&](const std::filesystem::path& p) {
        dep.built_archive = p.string();
        dep.is_shared = is_shared_lib(p);
        plan.ldflags.push_back(dep.built_archive);
        if (dep.is_shared) {
            plan.ldflags.push_back("-Wl,-rpath," + p.parent_path().string());
        }
    };

    // 2A. If dep.driver == "Make" and Makefile exists and `make` binary is installed, run Make build!
    if ((dep.driver == "Make" || dep.driver == "Autotools") &&
        (std::filesystem::exists(src_dir / "Makefile") || std::filesystem::exists(src_dir / "makefile"))) {
        int has_make = 0;
        (void)bridge::run_command_capture("which make", has_make);
        if (has_make == 0) {
            auto make_build_dir = cache_root / "make" / dep.name;
            std::filesystem::create_directories(make_build_dir);
            std::string make_cmd = std::format("make -C \"{}\" BUILD_DIR=\"{}\"", src_dir.string(), make_build_dir.string());
            std::string extra_cflags;
            for (const auto& [k, v] : dep.defines) {
                std::string val = (v == "true") ? "1" : v;
                make_cmd += std::format(" {}={}", k, val);
                extra_cflags += std::format(" -D{}={}", k, val);
            }
            if (!extra_cflags.empty()) {
                make_cmd += std::format(" EXTRA_CFLAGS=\"{}\"", extra_cflags);
            }
            int rc = 0;
            std::string out = bridge::run_command_capture(make_cmd, rc);
            if (rc != 0) {
                return std::unexpected(std::format("Make build failed for '{}':\n{}", dep.name, out));
            }
            for (const auto& search_root : {make_build_dir, src_dir}) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(search_root)) {
                    if (entry.is_regular_file() && is_lib_file(entry.path())) {
                        register_lib(entry.path());
                        return {};
                    }
                }
            }
        }
    }

    // 2B. If dep.driver == "CMake" and CMakeLists.txt exists and `cmake` binary is installed, run CMake build!
    if (dep.driver == "CMake" && std::filesystem::exists(src_dir / "CMakeLists.txt")) {
        int has_cmake = 0;
        (void)bridge::run_command_capture("which cmake", has_cmake);
        if (has_cmake == 0) {
            auto cmake_build_dir = cache_root / "cmake" / dep.name;
            std::filesystem::create_directories(cmake_build_dir);
            std::string cfg_cmd = std::format("cmake -S \"{}\" -B \"{}\"", src_dir.string(), cmake_build_dir.string());
            for (const auto& [k, v] : dep.defines) {
                cfg_cmd += std::format(" -D{}={}", k, v);
            }
            int rc = 0;
            std::string out = bridge::run_command_capture(cfg_cmd + " && cmake --build \"" + cmake_build_dir.string() + "\"", rc);
            if (rc != 0) {
                return std::unexpected(std::format("CMake build failed for '{}':\n{}", dep.name, out));
            }
            for (const auto& entry : std::filesystem::recursive_directory_iterator(cmake_build_dir)) {
                if (entry.is_regular_file() && is_lib_file(entry.path())) {
                    register_lib(entry.path());
                }
            }
            if (!dep.built_archive.empty()) return {};
        }
    }

    // 2C. If dep.driver == "Ninja" and build.ninja exists and `ninja` binary is installed, run Ninja build!
    if (dep.driver == "Ninja" && std::filesystem::exists(src_dir / "build.ninja")) {
        int has_ninja = 0;
        (void)bridge::run_command_capture("which ninja", has_ninja);
        if (has_ninja == 0) {
            int rc = 0;
            std::string out = bridge::run_command_capture(std::format("ninja -C \"{}\"", src_dir.string()), rc);
            if (rc != 0) {
                return std::unexpected(std::format("Ninja build failed for '{}':\n{}", dep.name, out));
            }
            for (const auto& entry : std::filesystem::recursive_directory_iterator(src_dir)) {
                if (entry.is_regular_file() && is_lib_file(entry.path())) {
                    register_lib(entry.path());
                    return {};
                }
            }
        }
    }

    // 3. Collect source files (.c / .cpp / .cc) from `dep.sources` or `<src_dir>` / `<src_dir>/src`
    std::vector<std::filesystem::path> src_files;
    if (!dep.sources.empty()) {
        for (const auto& rel : dep.sources) {
            src_files.push_back(src_dir / rel);
        }
    } else {
        auto collect_dir = [&](const std::filesystem::path& d) {
            if (!std::filesystem::is_directory(d)) return;
            for (const auto& entry : std::filesystem::directory_iterator(d)) {
                if (!entry.is_regular_file()) continue;
                auto ext = entry.path().extension();
                if (ext == ".c" || ext == ".cpp" || ext == ".cc") {
                    src_files.push_back(entry.path());
                }
            }
        };
        collect_dir(src_dir);
        collect_dir(src_dir / "src");
    }
    if (src_files.empty()) return {};

    bool build_shared = dep.is_shared || dep.defines.contains("shared") || dep.defines.contains("SHARED");
    std::vector<std::filesystem::path> obj_files;
    bool any_obj_recompiled = false;
    for (const auto& sf : src_files) {
        // Disambiguate identical stems across folders by hashing the relative path
        std::error_code ec;
        auto rel_p = std::filesystem::relative(sf, src_dir, ec);
        std::string rel_str = ec ? sf.string() : rel_p.string();
        std::size_t h = std::hash<std::string>{}(rel_str);
        auto obj_path = obj_dir / std::format("{}_{:08x}.o", sf.stem().string(), static_cast<std::uint32_t>(h & 0xFFFFFFFF));

        // Incremental compilation check: skip clang if object is up to date
        bool need_compile = true;
        if (std::filesystem::exists(obj_path, ec)) {
            auto src_time = std::filesystem::last_write_time(sf, ec);
            if (!ec) {
                auto obj_time = std::filesystem::last_write_time(obj_path, ec);
                if (!ec && obj_time >= src_time) {
                    need_compile = false;
                }
            }
        }

        if (need_compile) {
            any_obj_recompiled = true;
            bool is_cpp = (sf.extension() != ".c");
            std::string cc = is_cpp ? "clang++ -std=c++2b" : "clang -std=c17";
            if (!plan.target_triple.empty()) cc += " --target=" + plan.target_triple;
            if (!plan.sysroot.empty()) cc += " --sysroot=" + plan.sysroot;
            if (build_shared) cc += " -fPIC";
            std::string cmd = std::format("{} -O2 -c \"{}\" -o \"{}\"", cc, sf.string(), obj_path.string());

            for (const auto& inc : dep.includes) {
                auto inc_path = (inc == ".") ? src_dir : (src_dir / inc);
                cmd += std::format(" -I\"{}\"", inc_path.string());
            }
            if (dep.includes.empty()) {
                cmd += std::format(" -I\"{}\"", src_dir.string());
                if (std::filesystem::is_directory(src_dir / "include")) {
                    cmd += std::format(" -I\"{}\"", (src_dir / "include").string());
                }
            }
            for (const auto& [k, v] : dep.defines) {
                if (v == "true") {
                    cmd += std::format(" -D{}=1", k);
                } else if (v != "false") {
                    cmd += std::format(" -D{}={}", k, v);
                }
            }

            int rc = 0;
            std::string out = bridge::run_command_capture(cmd, rc);
            if (rc != 0) {
                return std::unexpected(std::format("failed to compile dependency '{}' source '{}':\n{}",
                    dep.name, sf.string(), out));
            }
        }
        obj_files.push_back(std::move(obj_path));
    }

    std::string lib_ext = build_shared ?
#if defined(__APPLE__)
        ".dylib"
#else
        ".so"
#endif
        : ".a";
    auto archive_path = lib_dir / std::format("lib{}{}", dep.name, lib_ext);
    bool need_archive = any_obj_recompiled || !std::filesystem::exists(archive_path);
    if (!need_archive) {
        std::error_code ec;
        auto ar_time = std::filesystem::last_write_time(archive_path, ec);
        if (ec) {
            need_archive = true;
        } else {
            for (const auto& ob : obj_files) {
                auto ob_time = std::filesystem::last_write_time(ob, ec);
                if (ec || ob_time > ar_time) {
                    need_archive = true;
                    break;
                }
            }
        }
    }

    if (need_archive) {
        std::filesystem::remove(archive_path);
        std::string link_cmd;
        if (build_shared) {
            link_cmd = std::format("clang++ -shared -o \"{}\"", archive_path.string());
            if (!plan.target_triple.empty()) link_cmd += " --target=" + plan.target_triple;
            if (!plan.sysroot.empty()) link_cmd += " --sysroot=" + plan.sysroot;
            for (const auto& ob : obj_files) {
                link_cmd += std::format(" \"{}\"", ob.string());
            }
        } else {
            link_cmd = std::format("ar rcs \"{}\"", archive_path.string());
            for (const auto& ob : obj_files) {
                link_cmd += std::format(" \"{}\"", ob.string());
            }
        }
        int ar_rc = 0;
        std::string ar_out = bridge::run_command_capture(link_cmd, ar_rc);
        if (ar_rc != 0) {
            return std::unexpected(std::format("failed to produce library for dependency '{}':\n{}", dep.name, ar_out));
        }
    }

    register_lib(archive_path);
    return {};
}

[[nodiscard]] inline auto execute_pre_build_command(
    const PreBuildCommandSpec& cmd,
    const std::filesystem::path& default_wd
) -> std::expected<void, std::string> {
    std::filesystem::path wd = cmd.work_dir.empty() ? default_wd : std::filesystem::path(cmd.work_dir);
    bool needs_run = false;
    if (cmd.outputs.empty()) {
        needs_run = true;
    } else {
        std::filesystem::file_time_type min_output_time = std::filesystem::file_time_type::max();
        for (const auto& out : cmd.outputs) {
            std::filesystem::path outp = wd / out;
            if (!std::filesystem::exists(outp)) {
                needs_run = true;
                break;
            }
            std::error_code ec;
            auto t = std::filesystem::last_write_time(outp, ec);
            if (!ec && t < min_output_time) min_output_time = t;
        }
        if (!needs_run) {
            for (const auto& in : cmd.inputs) {
                std::filesystem::path inp = wd / in;
                if (std::filesystem::exists(inp)) {
                    std::error_code ec;
                    auto t = std::filesystem::last_write_time(inp, ec);
                    if (!ec && t > min_output_time) {
                        needs_run = true;
                        break;
                    }
                }
            }
        }
    }

    if (needs_run) {
        std::string cmd_str = "cd \"" + wd.string() + "\" && " + cmd.command;
        for (const auto& arg : cmd.args) {
            if (arg.find(' ') != std::string::npos || arg.find('\t') != std::string::npos || arg.find('\n') != std::string::npos) {
                std::string esc;
                for (char c : arg) {
                    if (c == '"' || c == '\\') esc.push_back('\\');
                    esc.push_back(c);
                }
                cmd_str += " \"" + esc + "\"";
            } else {
                cmd_str += " " + arg;
            }
        }
        int code = 0;
        std::string output = bridge::run_command_capture(cmd_str, code);
        if (code != 0) {
            return std::unexpected(std::format("pre-build command failed (exit code {}): {}\n{}", code, cmd_str, output));
        }
    }
    return {};
}

// Recursively resolves local `.cf` imports (e.g. `import "./pkg/checksum"`) and merges their declarations.
[[nodiscard]] inline auto resolve_local_cf_imports(
    const std::filesystem::path& root_base_dir,
    frontend::FileAST& root_ast,
    BuildPlan& plan
) -> std::expected<void, std::string> {
    std::vector<frontend::ImportDecl> kept_imports;
    std::unordered_set<std::string> visited_files;
    std::unordered_set<std::string> visited_build_cfs;
    std::unordered_set<std::string> seen_non_local_imports;

    struct WorkItem {
        std::filesystem::path base_dir;
        frontend::ImportDecl imp;
    };
    std::deque<WorkItem> queue;

    for (const auto& imp : root_ast.imports) {
        queue.push_back({root_base_dir, imp});
    }

    while (!queue.empty()) {
        auto item = std::move(queue.front());
        queue.pop_front();

        const auto& imp = item.imp;
        const auto& base_dir = item.base_dir;

        if (!imp.is_cpp && (imp.path.starts_with("./") || imp.path.starts_with("../"))) {
            std::filesystem::path target = base_dir / imp.path;
            std::vector<std::filesystem::path> cf_files;
            std::filesystem::path pkg_dir;

            // Subpackage build.cf check (BS-07 & BS-08)
            std::filesystem::path candidate_dir = std::filesystem::is_directory(target) ? target : target.parent_path();
            if (!candidate_dir.empty()) {
                auto sub_build_cf = candidate_dir / "build.cf";
                std::string sub_bcf_canon = std::filesystem::weakly_canonical(sub_build_cf).string();
                if (std::filesystem::exists(sub_build_cf) && !visited_build_cfs.contains(sub_bcf_canon)) {
                    visited_build_cfs.insert(sub_bcf_canon);
                    auto parsed = parse_build_cf(sub_build_cf);
                    if (!parsed) return std::unexpected(parsed.error());
                    for (auto& dep : parsed->external_deps) {
                        bool exists_already = false;
                        for (const auto& ed : plan.external_deps) {
                            if (ed.name == dep.name) { exists_already = true; break; }
                        }
                        if (!exists_already) {
                            plan.external_deps.push_back(std::move(dep));
                        }
                    }
                    for (auto& cmd : parsed->pre_build_commands) {
                        if (cmd.work_dir.empty()) cmd.work_dir = candidate_dir.string();
                        if (auto r = execute_pre_build_command(cmd, candidate_dir); !r) {
                            return std::unexpected(r.error());
                        }
                        plan.pre_build_commands.push_back(std::move(cmd));
                    }
                }
            }

            if (std::filesystem::is_directory(target)) {
                pkg_dir = target;
                for (const auto& entry : std::filesystem::directory_iterator(target)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".cf" && entry.path().filename() != "build.cf") {
                        cf_files.push_back(entry.path());
                    }
                }
            } else if (std::filesystem::exists(target.string() + ".cf")) {
                cf_files.push_back(target.string() + ".cf");
                pkg_dir = target.parent_path();
            } else if (std::filesystem::exists(target)) {
                cf_files.push_back(target);
                pkg_dir = target.parent_path();
            } else {
                return std::unexpected(std::format("local package import not found: {}", imp.path));
            }

            plan.local_cf_packages.push_back(imp.path);

            for (const auto& fpath : cf_files) {
                std::string canon = std::filesystem::weakly_canonical(fpath).string();
                if (visited_files.contains(canon)) continue;
                visited_files.insert(canon);

                auto raw = read_file_text(fpath);
                if (!raw) return std::unexpected(raw.error());
                auto sub_ast = frontend::parse_source(fpath.string(), *raw);
                if (!sub_ast) return std::unexpected(sub_ast.error());

                // Recursively enqueue subpackage imports (BS-07)
                for (const auto& simp : sub_ast->imports) {
                    queue.push_back({fpath.parent_path(), simp});
                }

                // Preserve subpackage AST arena lifetime in root_ast
                if (sub_ast->arena) root_ast.child_arenas.push_back(sub_ast->arena);
                root_ast.child_arenas.insert(root_ast.child_arenas.end(),
                    sub_ast->child_arenas.begin(), sub_ast->child_arenas.end());

                // Merge AST declarations
                for (auto& st : sub_ast->structs) root_ast.structs.push_back(std::move(st));
                for (auto& en : sub_ast->enums) root_ast.enums.push_back(std::move(en));
                for (auto& ifc : sub_ast->interfaces) root_ast.interfaces.push_back(std::move(ifc));
                for (auto& gv : sub_ast->global_vars) root_ast.global_vars.push_back(std::move(gv));
                root_ast.functions.insert(root_ast.functions.begin(),
                    std::make_move_iterator(sub_ast->functions.begin()),
                    std::make_move_iterator(sub_ast->functions.end()));
                for (auto& td : sub_ast->tests) root_ast.tests.push_back(std::move(td));
            }
        } else {
            // Deduplicate non-local imports (C++ or core imports)
            std::string imp_key = (imp.is_cpp ? "cpp:" : "cf:") + imp.path + (imp.alias.empty() ? "" : ":" + imp.alias);
            if (!seen_non_local_imports.contains(imp_key)) {
                seen_non_local_imports.insert(imp_key);
                kept_imports.push_back(imp);
            }
        }
    }

    root_ast.imports = std::move(kept_imports);
    return {};
}

[[nodiscard]] inline auto analyze_and_emit(
    const std::filesystem::path& cf_file,
    const std::filesystem::path& runtime_dir,
    bool inspect_clang_ast,
    bool test_mode = false,
    const std::string& target_triple = "",
    const std::string& sysroot = ""
) -> std::expected<std::pair<std::string, BuildPlan>, std::string> {
    auto dir = cf_file.parent_path();
    BuildPlan plan;
    plan.entry_file = cf_file.string();
    plan.target_triple = target_triple;
    plan.sysroot = sysroot;
    plan.cflags = {"-std=c++2b", "-I" + runtime_dir.string()};
    if (!target_triple.empty()) {
        plan.cflags.push_back("--target=" + target_triple);
        plan.ldflags.push_back("--target=" + target_triple);
    }
    if (!sysroot.empty()) {
        plan.cflags.push_back("--sysroot=" + sysroot);
        plan.ldflags.push_back("--sysroot=" + sysroot);
    }

    // 1. Parse & execute any pre-build commands declared in root sibling `build.cf` (BS-08)
    auto build_cf = dir / "build.cf";
    if (cf_file.filename() != "build.cf" && std::filesystem::exists(build_cf)) {
        auto parsed = parse_build_cf(build_cf);
        if (!parsed) return std::unexpected(parsed.error());
        for (auto& dep : parsed->external_deps) {
            bool exists_already = false;
            for (const auto& ed : plan.external_deps) {
                if (ed.name == dep.name) { exists_already = true; break; }
            }
            if (!exists_already) {
                plan.external_deps.push_back(std::move(dep));
            }
        }
        for (auto& cmd : parsed->pre_build_commands) {
            if (cmd.work_dir.empty()) cmd.work_dir = dir.string();
            if (auto r = execute_pre_build_command(cmd, dir); !r) {
                return std::unexpected(r.error());
            }
            plan.pre_build_commands.push_back(std::move(cmd));
        }
    }

    // 2. Read and parse root .cf source
    auto raw = read_file_text(cf_file);
    if (!raw) return std::unexpected(raw.error());
    auto ast = frontend::parse_source(cf_file.string(), *raw);
    if (!ast) return std::unexpected(ast.error());

    // 3. Resolve local `.cf` package imports (`import "./..."`) recursively
    if (auto r = resolve_local_cf_imports(dir, *ast, plan); !r) {
        return std::unexpected(r.error());
    }

    // 4. Build all external dependencies (including those aggregated from subpackages)
    for (auto& dep : plan.external_deps) {
        if (auto r = build_local_dep(dir, dep, plan); !r) {
            return std::unexpected(r.error());
        }
    }

    // 3. Semantic Analysis
    auto checked = frontend::analyze(std::move(*ast));
    if (!checked) return std::unexpected(checked.error());

    static const std::unordered_map<std::string, std::string> core_to_pc = {
        {"core:compress/zstd", "libzstd"},
        {"core:json", "simdjson"},
        {"core:db/sqlite", "sqlite3"},
    };

    std::unordered_set<std::string> seen_pc;
    auto add_pc = [&](const std::string& pc) {
        if (seen_pc.contains(pc)) return;
        seen_pc.insert(pc);
        plan.pkg_config_deps.push_back(pc);
        query_pkg_config(pc, plan.cflags, plan.ldflags);
    };

    auto cache_dir = dir / ".cfire" / "cache";
    for (const auto& imp : checked->ast.imports) {
        if (!imp.is_cpp) {
            plan.auto_linked.push_back(imp.path);
            if (auto it = core_to_pc.find(imp.path); it != core_to_pc.end()) {
                add_pc(it->second);
            }
            if (imp.path == "vendor:raylib") {
                bool found_raylib = false;

                // 1. Try system pkg-config
                int chk_rc = 0;
                (void)bridge::run_command_capture("pkg-config --exists raylib 2>/dev/null", chk_rc);
                if (chk_rc == 0) {
                    add_pc("raylib");
                    found_raylib = true;
                }

                // 2. Try standard system directories
                if (!found_raylib) {
                    for (const auto& inc_dir : {"/opt/homebrew/include", "/usr/local/include", "/usr/include"}) {
                        if (std::filesystem::exists(std::filesystem::path(inc_dir) / "raylib.h")) {
                            plan.cflags.push_back("-I" + std::string(inc_dir));
                            std::string lib_dir = (inc_dir == std::string("/opt/homebrew/include")) ? "/opt/homebrew/lib" :
                                                  (inc_dir == std::string("/usr/local/include")) ? "/usr/local/lib" : "/usr/lib";
                            if (std::filesystem::exists(std::filesystem::path(lib_dir) / "libraylib.a")) {
                                plan.ldflags.push_back((std::filesystem::path(lib_dir) / "libraylib.a").string());
                            } else {
                                plan.ldflags.push_back("-L" + lib_dir);
                                plan.ldflags.push_back("-lraylib");
                            }
                            found_raylib = true;
                            break;
                        }
                    }
                }

                // 3. Try project-local vendor/raylib or ancestor vendor_raylib cache
                if (!found_raylib) {
                    for (auto cur = std::filesystem::absolute(dir); !cur.empty() && cur != cur.parent_path(); cur = cur.parent_path()) {
                        auto v_dir = cur / "vendor" / "raylib";
                        auto c_dir = cur / ".cfire" / "cache" / "vendor_raylib";
                        if (std::filesystem::exists(v_dir / "include" / "raylib.h")) {
                            plan.cflags.push_back("-I" + (v_dir / "include").string());
                            if (std::filesystem::exists(v_dir / "lib" / "libraylib.a")) {
                                plan.ldflags.push_back((v_dir / "lib" / "libraylib.a").string());
                            } else {
                                plan.ldflags.push_back("-L" + (v_dir / "lib").string());
                                plan.ldflags.push_back("-lraylib");
                            }
                            found_raylib = true;
                            break;
                        } else if (std::filesystem::exists(c_dir / "include" / "raylib.h")) {
                            plan.cflags.push_back("-I" + (c_dir / "include").string());
                            if (std::filesystem::exists(c_dir / "lib" / "libraylib.a")) {
                                plan.ldflags.push_back((c_dir / "lib" / "libraylib.a").string());
                            }
                            found_raylib = true;
                            break;
                        } else if (std::filesystem::exists(v_dir / "raylib.h")) {
                            plan.cflags.push_back("-I" + v_dir.string());
                            if (std::filesystem::exists(v_dir / "libraylib.a")) {
                                plan.ldflags.push_back((v_dir / "libraylib.a").string());
                            }
                            found_raylib = true;
                            break;
                        }
                    }
                }

                // 4. Try current working directory cache and global/local package cache
                if (!found_raylib) {
                    auto cwd_cache = std::filesystem::current_path() / ".cfire" / "cache" / "vendor_raylib";
                    if (std::filesystem::exists(cwd_cache / "include" / "raylib.h")) {
                        plan.cflags.push_back("-I" + (cwd_cache / "include").string());
                        if (std::filesystem::exists(cwd_cache / "lib" / "libraylib.a")) {
                            plan.ldflags.push_back((cwd_cache / "lib" / "libraylib.a").string());
                        }
                        found_raylib = true;
                    }
                }
                if (!found_raylib) {
                    auto cache_raylib = get_package_cache_root() / "vendor_raylib";
                    if (std::filesystem::exists(cache_raylib / "include" / "raylib.h")) {
                        plan.cflags.push_back("-I" + (cache_raylib / "include").string());
                        if (std::filesystem::exists(cache_raylib / "lib" / "libraylib.a")) {
                            plan.ldflags.push_back((cache_raylib / "lib" / "libraylib.a").string());
                        }
                        found_raylib = true;
                    }
                }

                // 5. If still not found, attempt automatic fetch from official release
                if (!found_raylib) {
                    auto cache_raylib = get_package_cache_root() / "vendor_raylib";
                    std::string raylib_url;
#if defined(__APPLE__)
                    raylib_url = "https://github.com/raysan5/raylib/releases/download/5.0/raylib-5.0_macos.tar.gz";
#elif defined(__linux__)
                    raylib_url = "https://github.com/raysan5/raylib/releases/download/5.0/raylib-5.0_linux_amd64.tar.gz";
#endif
                    if (!raylib_url.empty()) {
                        std::cout << "==> Auto-resolving missing vendor:raylib from " << raylib_url << "..." << std::endl;
                        std::string actual_sha;
                        auto fetch_res = fetch_archive(raylib_url, "", cache_raylib, actual_sha);
                        if (fetch_res && std::filesystem::exists(cache_raylib / "include" / "raylib.h")) {
                            plan.cflags.push_back("-I" + (cache_raylib / "include").string());
                            if (std::filesystem::exists(cache_raylib / "lib" / "libraylib.a")) {
                                plan.ldflags.push_back((cache_raylib / "lib" / "libraylib.a").string());
                            }
                            found_raylib = true;
                        }
                    }
                }

#if defined(__APPLE__)
                plan.ldflags.insert(plan.ldflags.end(), {"-framework", "Cocoa", "-framework", "IOKit", "-framework", "OpenGL"});
#elif defined(__linux__)
                plan.ldflags.insert(plan.ldflags.end(), {"-lGL", "-lm", "-lpthread", "-ldl", "-lrt", "-lX11"});
#endif
            }
            continue;
        }
        for (const auto& attr : imp.attrs) {
            if (attr.name == "pkg" && !attr.args.empty()) {
                add_pc(attr.args[0]);
            }
        }
        if (inspect_clang_ast) {
            auto idx = bridge::inspect_header(imp.path, plan.cflags, cache_dir);
            if (!idx) return std::unexpected(idx.error());
            for (const auto& [sym_name, sym] : idx->symbols) {
                if (sym.kind == "FunctionDecl") {
                    checked->cpp_fn_params[sym_name] = sym.param_types;
                }
            }
            plan.cpp_headers[imp.path] = std::move(*idx);
        }
    }

    // 4. C++23 Code Generation (with Clang AST boundary coercions)
    std::string cpp_code = frontend::emit_cpp(*checked, test_mode);

    return std::make_pair(std::move(cpp_code), std::move(plan));
}

[[nodiscard]] inline auto ensure_prelude_pch(
    const std::filesystem::path& runtime_dir,
    bool sanitize
) -> std::string {
    auto prelude_hdr = runtime_dir / "cfire_prelude.hpp";
    std::error_code ec;
    if (!std::filesystem::exists(prelude_hdr, ec)) return "";

    auto pch_dir = std::filesystem::temp_directory_path() / "cfire_pch_cache";
    std::filesystem::create_directories(pch_dir, ec);
    std::string pch_name = sanitize ? "cfire_prelude_san.pch" : "cfire_prelude_opt.pch";
    auto pch_path = pch_dir / pch_name;

    bool needs_build = !std::filesystem::exists(pch_path, ec);
    if (!needs_build) {
        auto hdr_time = std::filesystem::last_write_time(prelude_hdr, ec);
        auto pch_time = std::filesystem::last_write_time(pch_path, ec);
        if (!ec && hdr_time > pch_time) {
            needs_build = true;
        }
    }

    if (needs_build) {
        std::string opt_flags = sanitize
            ? "-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined"
            : "-O2";
        std::string cmd = std::format(
            "clang++ -std=c++2b {} -x c++-header \"{}\" -o \"{}\"",
            opt_flags, prelude_hdr.string(), pch_path.string());
        int rc = 0;
        (void)bridge::run_command_capture(cmd, rc);
        if (rc != 0) {
            std::filesystem::remove(pch_path, ec);
            return "";
        }
    }
    return pch_path.string();
}

[[nodiscard]] inline auto verify_cpp_syntax_only(
    const std::filesystem::path& cf_file,
    std::string_view cpp_code,
    const BuildPlan& plan,
    const std::filesystem::path& runtime_dir
) -> std::expected<void, std::string> {
    auto gen_dir = cf_file.parent_path() / ".cfire" / "gen";
    std::error_code ec;
    std::filesystem::create_directories(gen_dir, ec);
    auto cpp_path = gen_dir / (cf_file.stem().string() + ".cpp");
    if (ec) {
        cpp_path = std::filesystem::temp_directory_path() / (cf_file.stem().string() + "_check.cpp");
    }
    {
        std::ofstream out(cpp_path, std::ios::binary | std::ios::trunc);
        out << cpp_code;
    }

    std::string pch_path;
    if (plan.target_triple.empty()) {
        pch_path = ensure_prelude_pch(runtime_dir, false);
    }
    std::string cmd = "clang++ -O2";
    for (const auto& f : plan.cflags) cmd += " " + f;
    if (!pch_path.empty()) {
        cmd += std::format(" -include-pch \"{}\"", pch_path);
    }
    cmd += std::format(" -fsyntax-only \"{}\" 2>&1", cpp_path.string());
    int rc = 0;
    auto output = bridge::run_command_capture(cmd, rc);
    if (rc != 0) {
        return std::unexpected(output);
    }
    return {};
}

[[nodiscard]] inline auto build_native(
    const std::filesystem::path& cf_file,
    const std::filesystem::path& runtime_dir,
    const std::filesystem::path& out_bin,
    bool test_mode = false,
    bool sanitize = false,
    const std::string& target_triple = "",
    const std::string& sysroot = "",
    const std::vector<std::string>& extra_link_libs = {},
    const std::vector<std::string>& extra_includes = {}
) -> std::expected<BuildPlan, std::string> {
    auto res = analyze_and_emit(cf_file, runtime_dir, true, test_mode, target_triple, sysroot);
    if (!res) return std::unexpected(res.error());
    auto [cpp_code, plan] = std::move(*res);
    plan.sanitized = sanitize;
    plan.extra_link_libs = extra_link_libs;
    plan.extra_includes = extra_includes;

    for (const auto& lib : extra_link_libs) {
        plan.ldflags.push_back(lib);
        plan.ldflags.push_back("-lpthread");
        plan.ldflags.push_back("-ldl");
        plan.ldflags.push_back("-lm");
    }
    for (const auto& inc : extra_includes) {
        plan.cflags.push_back("-I \"" + inc + "\"");
    }

    auto gen_dir = cf_file.parent_path() / ".cfire" / "gen";
    std::filesystem::create_directories(gen_dir);
    auto stem = test_mode ? (cf_file.stem().string() + "_test") : cf_file.stem().string();
    auto cpp_path = gen_dir / (stem + ".cpp");
    plan.generated_cpp = cpp_path.string();

    {
        std::ofstream out(cpp_path, std::ios::binary | std::ios::trunc);
        out << cpp_code;
    }

    std::string opt_flags = sanitize
        ? "-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined"
        : "-O2";
    // Only use host PCH if compiling natively for the host (empty target_triple)
    std::string pch_path;
    if (target_triple.empty()) {
        pch_path = ensure_prelude_pch(runtime_dir, sanitize);
    }

    std::string cmd = "clang++";
    for (const auto& f : plan.cflags) cmd += " " + f;
    if (!pch_path.empty()) {
        cmd += std::format(" -include-pch \"{}\"", pch_path);
        plan.used_pch = true;
    }
    cmd += std::format(" {} \"{}\" -o \"{}\"", opt_flags, cpp_path.string(), out_bin.string());
    for (const auto& l : plan.ldflags) cmd += " " + l;

    int exit_code = 0;
    std::string output = bridge::run_command_capture(cmd, exit_code);
    if (exit_code != 0) {
        return std::unexpected(std::format("clang++ compilation failed:\n{}\nCommand: {}", output, cmd));
    }
    return plan;
}

[[nodiscard]] inline auto generate_cmake_content(const BuildPlan& plan) -> std::string {
    std::ostringstream cmake;
    cmake << "cmake_minimum_required(VERSION 3.25)\n";
    cmake << "project(cfire_ejected_app LANGUAGES C CXX)\n";
    cmake << "set(CMAKE_CXX_STANDARD 23)\n";
    cmake << "set(CMAKE_CXX_STANDARD_REQUIRED OFF)\n\n";
    cmake << "include_directories(\"${CMAKE_CURRENT_SOURCE_DIR}\")\n\n";
    if (!plan.pkg_config_deps.empty()) {
        cmake << "find_package(PkgConfig REQUIRED)\n";
        for (std::size_t i = 0; i < plan.pkg_config_deps.size(); ++i) {
            cmake << std::format("pkg_check_modules(DEP_{} REQUIRED IMPORTED_TARGET {})\n", i, plan.pkg_config_deps[i]);
        }
        cmake << "\n";
    }
    for (const auto& dep : plan.external_deps) {
        std::string vendor_prefix = std::format("vendor/{}", dep.name);
        if (!dep.sources.empty()) {
            std::string srcs;
            for (const auto& s : dep.sources) {
                if (!srcs.empty()) srcs += " ";
                srcs += std::format("\"{}/{}\"", vendor_prefix, s);
            }
            cmake << std::format("add_library({} STATIC {})\n", dep.name, srcs);
            cmake << std::format("target_include_directories({} PUBLIC \"${{CMAKE_CURRENT_SOURCE_DIR}}/{}\")\n", dep.name, vendor_prefix);
            for (const auto& [k, v] : dep.defines) {
                cmake << std::format("target_compile_definitions({} PRIVATE {}={})\n", dep.name, k, v);
            }
            cmake << "\n";
        } else if (dep.driver == "CMake" || dep.driver == "cmake") {
            cmake << std::format("if (EXISTS \"${{CMAKE_CURRENT_SOURCE_DIR}}/{}/CMakeLists.txt\")\n", vendor_prefix);
            cmake << std::format("    add_subdirectory({})\n", vendor_prefix);
            cmake << "endif()\n\n";
        }
    }
    cmake << "add_executable(app main.cpp)\n";
    cmake << "if (CMAKE_CXX_COMPILER_ID MATCHES \"Clang|AppleClang\")\n";
    cmake << "    target_compile_options(app PRIVATE -std=c++2b)\n";
    cmake << "endif()\n";
    cmake << "target_include_directories(app PRIVATE \"${CMAKE_CURRENT_SOURCE_DIR}\")\n";
    for (std::size_t i = 0; i < plan.pkg_config_deps.size(); ++i) {
        cmake << std::format("target_link_libraries(app PRIVATE PkgConfig::DEP_{})\n", i);
    }
    for (const auto& dep : plan.external_deps) {
        cmake << std::format("target_link_libraries(app PRIVATE {})\n", dep.name);
    }
    for (const auto& flag : plan.ldflags) {
        cmake << std::format("target_link_libraries(app PRIVATE \"{}\")\n", flag);
    }
    for (const auto& al : plan.auto_linked) {
        if (al.starts_with("vendor:raylib")) {
            cmake << "target_link_libraries(app PRIVATE raylib)\n";
        }
    }
    return cmake.str();
}

[[nodiscard]] inline auto eject_cmake(
    const std::filesystem::path& cf_file,
    const std::filesystem::path& runtime_dir,
    const std::filesystem::path& out_dir
) -> std::expected<void, std::string> {
    auto res = analyze_and_emit(cf_file, runtime_dir, true);
    if (!res) return std::unexpected(res.error());
    auto [cpp_code, plan] = std::move(*res);

    std::filesystem::create_directories(out_dir);
    {
        std::ofstream out(out_dir / "main.cpp", std::ios::binary | std::ios::trunc);
        out << cpp_code;
    }
    for (const char* hdr : {"cfire_prelude.hpp", "cfire_core_stdlib.hpp"}) {
        std::error_code ec;
        std::filesystem::copy_file(runtime_dir / hdr, out_dir / hdr,
            std::filesystem::copy_options::overwrite_existing, ec);
    }
    if (std::filesystem::exists(runtime_dir / "core")) {
        std::error_code ec;
        std::filesystem::copy(runtime_dir / "core", out_dir / "core",
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
    }

    std::filesystem::path cf_dir = std::filesystem::absolute(cf_file).parent_path();
    for (const auto& dep : plan.external_deps) {
        if (!dep.source.empty()) {
            std::filesystem::path src_dir = dep.source.starts_with("/") ? std::filesystem::path(dep.source) : (cf_dir / dep.source);
            if (std::filesystem::exists(src_dir)) {
                std::filesystem::path dest_vendor = out_dir / "vendor" / dep.name;
                std::error_code ec;
                std::filesystem::create_directories(dest_vendor, ec);
                std::filesystem::copy(src_dir, dest_vendor,
                    std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
            }
        }
    }

    std::string cmake_text = generate_cmake_content(plan);
    std::ofstream cm_out(out_dir / "CMakeLists.txt", std::ios::binary | std::ios::trunc);
    cm_out << cmake_text;
    return {};
}

} // namespace cfire::build
