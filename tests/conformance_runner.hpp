#pragma once
// tests/conformance_runner.hpp — Language conformance corpus runner.
//
// Each tests/conformance/**/*.cf file pins one language expectation with leading `//` directives:
//
//     // expect: stdout              exact program stdout follows, one `// | <line>` per line
//     // | hello
//     // expect: error cfire/CODE @L[:C]     (repeatable) the exact set of errors `cfire check` must report
//     // expect: warning cfire/CODE @L       (repeatable) warnings that must be present
//     // expect: panic "<substring>" [@L]    program must panic (non-zero exit, stderr contains `panic`)
//     // expect: exit N                      program exit status (default 0, or non-zero with `panic`)
//     // expect: xfail <topic>: <reason>     ideal expectations above are NOT met yet
//     <code> //~ error cfire/CODE            inline form, preferred: expected error on this very line
//     <code> //~ warning cfire/CODE          inline expected warning
//     <code> //~ panic "<substring>"         inline expected panic attributed to this line
//     // flags: --sanitize | --sanitize=thread   extra run flags (thread falls back to emit-cpp + TSan)
//     // requires-lib: libsodium|libssh|libzstd|webkit2gtk   skip cleanly when optional C lib is absent
//
// Files named *_test.cf run with `cfire test` instead of `cfire run`.
// Outcomes: PASS, SKIP, FAIL, XFAIL (known hole, expected), XPASS (hole now fixed: drop the xfail line).
//
// Usage: .cfire_test_runner conformance [--filter glob] [--jobs N] [--run-flags=--sanitize] [--cfire path] [-v]

#include "process_run.hpp"
#include "test_harness.hpp"
#include <algorithm>
#include <optional>
#include <regex>
#include <set>
#include <tuple>

namespace conformance {

struct VirtualFile {
    std::string relpath{};
    std::string content{};
    bool is_entry = false;
};

struct Spec {
    std::optional<std::vector<std::string>> stdout_lines{};
    std::set<std::tuple<std::string, int, int>> errors{};  // (code, line, col or 0 = any)
    std::set<std::pair<std::string, int>> warnings{};
    std::optional<std::pair<std::string, int>> panic{};    // (substring, line or 0 = any)
    std::optional<int> exit{};
    bool exit_signal = false;
    std::string xfail{};
    std::vector<std::string> flags{};
    std::vector<std::string> requires_libs{};
    std::vector<VirtualFile> files{};
    std::set<std::tuple<std::string, std::string, int, int>> file_errors{};  // (relpath, code, line, col)
    std::set<std::tuple<std::string, std::string, int>> file_warnings{};     // (relpath, code, line)
    std::optional<std::tuple<std::string, std::string, int>> file_panic{};   // (relpath, sub, line)
};

struct DirectiveError {
    std::string message;
};

[[nodiscard]] inline auto split_lines(std::string_view text) -> std::vector<std::string> {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start < text.size()) {
        auto nl = text.find('\n', start);
        if (nl == std::string_view::npos) nl = text.size();
        std::string line(text.substr(start, nl - start));
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(std::move(line));
        start = nl + 1;
    }
    return out;
}

[[nodiscard]] inline auto trim(std::string_view s) -> std::string {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string_view::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(b, e - b + 1));
}

[[nodiscard]] inline auto split_ws(std::string_view s) -> std::vector<std::string> {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ' ' || c == '\t') {
            if (!cur.empty()) out.push_back(std::move(cur)), cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

[[nodiscard]] inline auto validate_virtual_relpath(std::string_view rel) -> bool {
    if (rel.empty()) return false;
    if (rel.front() == '/' || rel.front() == '\\' || rel.find('\\') != std::string_view::npos || rel.find(':') != std::string_view::npos) {
        return false;
    }
    std::size_t pos = 0;
    while (pos <= rel.size()) {
        auto slash = rel.find('/', pos);
        std::string_view seg = (slash == std::string_view::npos) ? rel.substr(pos) : rel.substr(pos, slash - pos);
        if (seg.empty() || seg == "." || seg == "..") return false;
        if (slash == std::string_view::npos) break;
        pos = slash + 1;
    }
    return true;
}

[[nodiscard]] inline auto normalize_slash_path(std::string_view p) -> std::string {
    std::string out(p);
    std::ranges::replace(out, '\\', '/');
    return out;
}

[[nodiscard]] inline auto parse_spec(std::string_view text) -> std::expected<Spec, DirectiveError> {
    static const std::regex kDiag(R"((cfire/[A-Z0-9-]+)\s+@(\d+)(?::(\d+))?)");
    static const std::regex kPanic(R"rx("([^"]*)"(?:\s+@(\d+))?)rx");
    static const std::regex kInlineDiag(R"(//~\s*(error|warning)\s+(cfire/[A-Z0-9-]+)\s*$)");
    static const std::regex kInlinePanic(R"rx(//~\s*panic\s+"([^"]*)"\s*$)rx");
    static const std::regex kXfailTopic(R"([a-z0-9][a-z0-9_-]*:\s+\S.*)");
    static const std::regex kFileMarker(R"(^//\s*---\s*file:\s*(.+?)\s*$)");
    auto bad = [](std::string m) { return std::unexpected(DirectiveError{std::move(m)}); };

    Spec spec;
    const auto lines = split_lines(text);
    std::size_t i = 0;
    std::smatch fm;
    while (i < lines.size() && lines[i].starts_with("//") && !std::regex_match(lines[i], fm, kFileMarker)) {
        const std::string body = trim(std::string_view(lines[i]).substr(2));
        if (body.starts_with("expect:")) {
            const std::string rest = trim(std::string_view(body).substr(7));
            const auto sp = rest.find(' ');
            const std::string kind = rest.substr(0, sp);
            const std::string arg = sp == std::string::npos ? "" : trim(std::string_view(rest).substr(sp + 1));
            std::smatch m;
            if (kind == "stdout") {
                spec.stdout_lines.emplace();
                while (i + 1 < lines.size() && lines[i + 1].starts_with("// |")) {
                    ++i;
                    std::string_view l = std::string_view(lines[i]).substr(4);
                    if (l.starts_with(' ')) l.remove_prefix(1);
                    spec.stdout_lines->emplace_back(l);
                }
            } else if (kind == "error" || kind == "warning") {
                if (!std::regex_match(arg, m, kDiag)) return bad(std::format("bad {} directive: '{}'", kind, body));
                const int line = std::stoi(m[2].str());
                if (kind == "error") spec.errors.emplace(m[1].str(), line, m[3].matched ? std::stoi(m[3].str()) : 0);
                else spec.warnings.emplace(m[1].str(), line);
            } else if (kind == "panic") {
                if (!std::regex_match(arg, m, kPanic)) return bad(std::format("bad panic directive: '{}'", body));
                spec.panic = std::pair{m[1].str(), m[2].matched ? std::stoi(m[2].str()) : 0};
            } else if (kind == "exit") {
                if (arg == "signal") {
                    spec.exit_signal = true;
                } else {
                    try {
                        spec.exit = std::stoi(arg);
                    } catch (...) {
                        return bad(std::format("bad exit directive: '{}'", body));
                    }
                }
            } else if (kind == "xfail") {
                if (!std::regex_match(arg, kXfailTopic)) return bad(std::format("xfail must specify '<topic>: <reason>': '{}'", body));
                spec.xfail = arg;
            } else {
                return bad(std::format("unknown expectation '{}'", kind));
            }
        } else if (body.starts_with("flags:")) {
            spec.flags = split_ws(std::string_view(body).substr(6));
        } else if (body.starts_with("requires-lib:")) {
            auto libs = split_ws(std::string_view(body).substr(13));
            if (libs.empty()) return bad(std::format("requires-lib must name at least one library: '{}'", body));
            for (const auto& lib : libs) {
                if (lib != "libsodium" && lib != "libssh" && lib != "libzstd" && lib != "webkit2gtk") {
                    return bad(std::format("unknown library '{}' in requires-lib", lib));
                }
                spec.requires_libs.push_back(lib);
            }
        }
        ++i;
    }

    bool has_file_markers = false;
    for (std::size_t n = i; n < lines.size(); ++n) {
        if (std::regex_match(lines[n], fm, kFileMarker)) {
            has_file_markers = true;
            break;
        }
    }

    if (has_file_markers) {
        std::set<std::string> seen_paths;
        bool seen_entry = false;
        int local_lineno = 0;
        for (std::size_t n = 0; n < lines.size(); ++n) {
            if (std::regex_match(lines[n], fm, kFileMarker)) {
                std::string raw_spec = trim(fm[1].str());
                bool is_entry = false;
                constexpr std::string_view kEntryTag = "(entry)";
                if (raw_spec.ends_with(kEntryTag)) {
                    is_entry = true;
                    raw_spec = trim(std::string_view(raw_spec).substr(0, raw_spec.size() - kEntryTag.size()));
                }
                if (!validate_virtual_relpath(raw_spec)) {
                    return bad(std::format("invalid virtual file path '{}' on line {}", raw_spec, n + 1));
                }
                if (!seen_paths.insert(raw_spec).second) {
                    return bad(std::format("duplicate virtual file path '{}' on line {}", raw_spec, n + 1));
                }
                if (is_entry) {
                    if (seen_entry) return bad("multiple '(entry)' virtual files declared");
                    seen_entry = true;
                }
                spec.files.push_back(VirtualFile{raw_spec, "", is_entry});
                local_lineno = 0;
                continue;
            }
            if (spec.files.empty()) {
                std::string trimmed_line = trim(lines[n]);
                if (!trimmed_line.empty() && !trimmed_line.starts_with("//")) {
                    return bad(std::format("unexpected code outside '// --- file:' section on line {}", n + 1));
                }
                continue;
            }
            ++local_lineno;
            spec.files.back().content += lines[n];
            spec.files.back().content.push_back('\n');
            std::smatch m;
            if (std::regex_search(lines[n], m, kInlineDiag)) {
                if (m[1].str() == "error") {
                    spec.errors.emplace(m[2].str(), local_lineno, 0);
                    spec.file_errors.emplace(spec.files.back().relpath, m[2].str(), local_lineno, 0);
                } else {
                    spec.warnings.emplace(m[2].str(), local_lineno);
                    spec.file_warnings.emplace(spec.files.back().relpath, m[2].str(), local_lineno);
                }
                continue;
            }
            if (std::regex_search(lines[n], m, kInlinePanic)) {
                if (spec.panic) return bad("more than one panic expectation");
                spec.panic = std::pair{m[1].str(), local_lineno};
                spec.file_panic = std::tuple{spec.files.back().relpath, m[1].str(), local_lineno};
            } else if (lines[n].contains("//~")) {
                return bad(std::format("bad inline annotation in '{}' on line {}: '{}'", spec.files.back().relpath, local_lineno, trim(lines[n])));
            }
        }
        if (!spec.errors.empty() && spec.file_errors.empty()) {
            return bad("header 'expect: error' cannot be used without '//~ error' in multi-file specs");
        }
    } else {
        // Inline annotations travel with the code they describe, so adding or removing header directives
        // (e.g. dropping an `xfail` line) never invalidates expected line numbers.
        for (std::size_t n = 0; n < lines.size(); ++n) {
            const int lineno = static_cast<int>(n) + 1;
            std::smatch m;
            if (std::regex_search(lines[n], m, kInlineDiag)) {
                if (m[1].str() == "error") spec.errors.emplace(m[2].str(), lineno, 0);
                else spec.warnings.emplace(m[2].str(), lineno);
                continue;
            }
            if (std::regex_search(lines[n], m, kInlinePanic)) {
                if (spec.panic) return bad("more than one panic expectation");
                spec.panic = std::pair{m[1].str(), lineno};
            } else if (lines[n].contains("//~")) {
                return bad(std::format("bad inline annotation on line {}: '{}'", lineno, trim(lines[n])));
            }
        }
    }
    if (!spec.stdout_lines && spec.errors.empty() && !spec.panic && !spec.exit && !spec.exit_signal && spec.warnings.empty()) {
        return bad("file declares no expectation");
    }
    return spec;
}

// clang's Windows targets have no ThreadSanitizer. Files that ask for it still run there, unsanitized,
// so their output is checked; the summary notes how many ran without TSan.
#if defined(_WIN32)
inline constexpr bool kTsanUnsupported = true;
#else
inline constexpr bool kTsanUnsupported = false;
#endif

struct Options {
    std::string cfire{};                  // absolute path of the cfire binary
    std::filesystem::path runtime_dir{};  // for the TSan fallback's -I
    int timeout = 300;
    std::vector<std::string> run_flags{};
    bool strict = false;                  // fail non-zero if any conformance test is skipped
};

[[nodiscard]] inline auto remediation_hint_for_lib(std::string_view lib) -> std::string {
#if defined(__APPLE__)
    if (lib == "libsodium") return "brew install libsodium (or 'cfire setup --yes')";
    if (lib == "libssh") return "brew install libssh (or 'cfire setup --yes')";
    if (lib == "libzstd") return "brew install zstd (or 'cfire setup --yes')";
    if (lib == "webkit2gtk") return "N/A on macOS (uses native WKWebView)";
#elif defined(_WIN32)
    if (lib == "libsodium") return "pacman -S mingw-w64-clang-x86_64-libsodium";
    if (lib == "libssh") return "pacman -S mingw-w64-clang-x86_64-libssh";
    if (lib == "libzstd") return "pacman -S mingw-w64-clang-x86_64-zstd";
    if (lib == "webkit2gtk") return "N/A on Windows (uses native WebView2)";
#else
    if (lib == "libsodium") return "sudo apt-get install -y libsodium-dev (or 'cfire setup --yes')";
    if (lib == "libssh") return "sudo apt-get install -y libssh-dev (or 'cfire setup --yes')";
    if (lib == "libzstd") return "sudo apt-get install -y libzstd-dev (or 'cfire setup --yes')";
    if (lib == "webkit2gtk") return "sudo apt-get install -y libwebkit2gtk-4.1-dev";
#endif
    return "run 'cfire setup --yes' to install missing optional native libraries";
}

[[nodiscard]] inline auto host_has_optional_lib(std::string_view lib) -> bool {
    if (const char* forced = std::getenv("CFIRE_TEST_FORCE_MISSING_LIB"); forced && *forced) {
        std::string_view fv(forced);
        if (fv == lib) return false;
    }
    auto has_hdr_or_pc = [](std::string_view pc_name, std::string_view rel_hdr) -> bool {
        if (cfire::build::ProbeCache::instance().probe_pkg_config(std::string(pc_name)).found) {
            return true;
        }
        std::error_code ec;
        if (const char* home = std::getenv("HOME"); home && *home) {
            if (std::filesystem::exists(std::filesystem::path(home) / ".local" / "include" / rel_hdr, ec)) {
                return true;
            }
        }
        if (const char* mp = std::getenv("MINGW_PREFIX"); mp && *mp) {
            if (std::filesystem::exists(std::filesystem::path(mp) / "include" / rel_hdr, ec)) {
                return true;
            }
        }
        for (const char* dir : {"/opt/homebrew/include", "/usr/local/include", "/usr/include", "/clang64/include", "C:/msys64/clang64/include"}) {
            if (std::filesystem::exists(std::filesystem::path(dir) / rel_hdr, ec)) {
                return true;
            }
        }
        return false;
    };
    if (lib == "libsodium") return has_hdr_or_pc("libsodium", "sodium.h");
    if (lib == "libssh") return has_hdr_or_pc("libssh", "libssh/libssh.h");
    if (lib == "libzstd") return has_hdr_or_pc("libzstd", "zstd.h");
    if (lib == "webkit2gtk") {
#if defined(__APPLE__) || defined(_WIN32)
        return true;
#else
        return cfire::build::ProbeCache::instance().probe_pkg_config("webkit2gtk-4.1").found ||
               cfire::build::ProbeCache::instance().probe_pkg_config("webkit2gtk-4.0").found;
#endif
    }
    return false;
}

struct Evaluation {
    Spec spec{};
    bool met = false;
    std::vector<std::string> reasons{};
    bool tsan_skipped = false;
    std::string skipped_missing_lib{};
};

[[nodiscard]] inline auto has_flag(const std::vector<std::string>& flags, std::string_view f) -> bool {
    return std::ranges::find(flags, f) != flags.end();
}

[[nodiscard]] inline auto tail(std::string_view s, std::size_t n) -> std::string {
    std::string t = trim(s);
    return t.size() > n ? t.substr(t.size() - n) : t;
}

inline auto run_program(const Options& opt, const std::filesystem::path& work, const std::string& name, Spec& spec, bool is_test,
                        bool& tsan_skipped) -> ProcResult {
    if (has_flag(spec.flags, "--sanitize=thread") && kTsanUnsupported) {
        tsan_skipped = true;
        std::erase(spec.flags, std::string("--sanitize=thread"));
    }
    if (has_flag(spec.flags, "--sanitize=thread") && !is_test) {
        auto r = run_process({opt.cfire, "run", "--no-cache", "--sanitize=thread", name}, work, opt.timeout);
        if (!r.err.contains("unrecognized flag") && !r.out.contains("unrecognized flag")) return r;
        // Fallback until adds a native mode.
        auto cpp = run_process({opt.cfire, "emit-cpp", name}, work, opt.timeout);
        if (cpp.exit_code != 0 || cpp.timed_out) return cpp;
        (void)cfire::fs::write_text(work / "prog.cpp", cpp.out);
        const char* cxx_env = std::getenv("CXX");
        std::string cxx = (cxx_env != nullptr && *cxx_env != '\0') ? cxx_env : "clang++";
        auto cc = run_process({cxx, "-std=c++2b", "-O1", "-g", "-fsanitize=thread", "-I" + opt.runtime_dir.string(), "prog.cpp", "-o",
                               "prog_tsan", "-pthread"},
                              work, opt.timeout);
        if (cc.exit_code != 0 || cc.timed_out) return cc;
        return run_process({(work / "prog_tsan").string()}, work, opt.timeout);
    }
    std::vector<std::string> argv{opt.cfire, is_test ? "test" : "run", "--no-cache"};
    for (const auto& f : spec.flags) {
        if (f != "--sanitize=thread") argv.push_back(f);
    }
    argv.push_back(name);
    return run_process(argv, work, opt.timeout);
}

[[nodiscard]] inline auto evaluate(const Options& opt, const std::filesystem::path& path) -> std::expected<Evaluation, DirectiveError> {
    auto text = cfire::fs::read_text(path);
    if (!text) return std::unexpected(DirectiveError{"cannot read file"});
    auto parsed = parse_spec(*text);
    if (!parsed) return std::unexpected(parsed.error());
    Evaluation ev;
    ev.spec = std::move(*parsed);
    Spec& spec = ev.spec;
    for (const auto& lib : spec.requires_libs) {
        if (!host_has_optional_lib(lib)) {
            ev.skipped_missing_lib = lib;
            return ev;
        }
    }
    for (const auto& f : opt.run_flags) {
        if (!has_flag(spec.flags, f)) spec.flags.push_back(f);
    }
    auto& reasons = ev.reasons;
    const std::string fname = path.filename().string();
    ScopedTempDir work("cfire_conf");
    std::string name;
    bool is_test = fname.ends_with("_test.cf");
    std::error_code ec;
    if (!spec.files.empty()) {
        const VirtualFile* entry_vf = nullptr;
        for (const auto& vf : spec.files) {
            if (vf.is_entry) { entry_vf = &vf; break; }
        }
        if (!entry_vf) {
            for (const auto& vf : spec.files) {
                if (vf.relpath == "main_test.cf" || vf.relpath == "main.cf") {
                    entry_vf = &vf;
                    break;
                }
            }
        }
        if (!entry_vf) entry_vf = &spec.files.back();
        for (const auto& vf : spec.files) {
            auto dst = work.path / std::filesystem::path(vf.relpath);
            if (dst.has_parent_path()) std::filesystem::create_directories(dst.parent_path(), ec);
            (void)cfire::fs::write_text(dst, vf.content);
        }
        name = entry_vf->relpath;
        is_test = is_test || entry_vf->relpath.ends_with("_test.cf");
    } else {
        name = is_test ? "main_test.cf" : "main.cf";
        std::filesystem::copy_file(path, work.path / name, std::filesystem::copy_options::overwrite_existing, ec);
    }

    const bool needs_run = static_cast<bool>(spec.stdout_lines || spec.panic || spec.exit || spec.exit_signal);
    const bool verify_cpp_in_check = !(needs_run && spec.errors.empty());
    std::vector<std::string> chk_argv{opt.cfire, "check"};
    if (verify_cpp_in_check) chk_argv.push_back("--verify-cpp");
    chk_argv.push_back("--json");
    chk_argv.push_back(name);
    auto chk = run_process(chk_argv, work.path, opt.timeout);
    if (chk.timed_out) {
        reasons.push_back("check timed out");
        return ev;
    }
    if (chk.spawn_failed || chk.exit_code < 0 || chk.exit_code >= 128) {
        reasons.push_back(std::format("cfire check crashed (rc={}): {}", chk.exit_code, tail(chk.err, 200)));
        return ev;
    }
    const auto brace = chk.out.find('{');
    std::optional<cfire::json::Value> report;
    if (brace != std::string::npos) {
        if (auto v = cfire::json::parse_dynamic(std::string_view(chk.out).substr(brace))) report = std::move(*v);
    }
    if (!report || !report->is_object()) {
        reasons.push_back(std::format("unparseable check --json output: {}", (chk.out + chk.err).substr(0, 200)));
        return ev;
    }
    auto match_virtual_rel = [&](std::string raw_file) -> std::string {
        std::string norm = normalize_slash_path(raw_file);
        if (norm.empty()) return name;
        for (const auto& vf : spec.files) {
            if (norm == vf.relpath || norm.ends_with("/" + vf.relpath)) return vf.relpath;
        }
        return norm;
    };
    std::set<std::tuple<std::string, int, int>> got_errors;
    std::set<std::pair<std::string, int>> got_warnings;
    std::set<std::tuple<std::string, std::string, int>> got_file_errors;
    std::set<std::tuple<std::string, std::string, int>> got_file_warnings;
    if (auto diags = report->get("diagnostics"); diags && diags->is_array()) {
        for (const auto& d : diags->as_array()) {
            auto str_of = [&](std::string_view k) { auto v = d.get(k); return v && v->is_string() ? std::string(v->as_str()) : std::string(); };
            auto int_of = [&](std::string_view k) { auto v = d.get(k); return v && v->is_number() ? static_cast<int>(v->as_int()) : 0; };
            const auto sev = str_of("severity");
            const auto code = str_of("code");
            const int dline = int_of("line");
            const int dcol = int_of("col");
            const auto relf = match_virtual_rel(str_of("file"));
            if (sev == "error") {
                got_errors.emplace(code, dline, dcol);
                got_file_errors.emplace(relf, code, dline);
            } else if (sev == "warning") {
                got_warnings.emplace(code, dline);
                got_file_warnings.emplace(relf, code, dline);
            }
        }
    }

    // ICE gate: `cfire/TYPE-ERROR` or `cfire/INTERNAL-BACKEND-ERROR` means clang rejected generated C++
    // for a program Sema accepted. It is never an acceptable expectation; Sema must report a cfire diagnostic instead.
    auto is_backend_ice = [](std::string_view code) {
        return code == "cfire/TYPE-ERROR" || code == "cfire/INTERNAL-BACKEND-ERROR";
    };
    for (const auto& [c, l, col] : spec.errors) {
        if (is_backend_ice(c)) return std::unexpected(DirectiveError{std::format("{} (a clang backend error) can't be an expected diagnostic", c)});
    }
    for (const auto& [c, l, col] : got_errors) {
        if (is_backend_ice(c))
            reasons.push_back(std::format("internal compiler error: clang rejected generated C++ at line {} for a program Sema accepted ({})", l, c));
    }
    std::erase_if(got_errors, [&](const auto& e) { return is_backend_ice(std::get<0>(e)); });
    std::erase_if(got_file_errors, [&](const auto& e) { return is_backend_ice(std::get<1>(e)); });
    std::set<std::pair<std::string, int>> want, have;
    for (const auto& [c, l, col] : spec.errors) want.emplace(c, l);
    for (const auto& [c, l, col] : got_errors) have.emplace(c, l);
    if (!spec.files.empty()) {
        std::set<std::tuple<std::string, std::string, int>> want_f;
        for (const auto& [rf, c, l, col] : spec.file_errors) want_f.emplace(rf, c, l);
        auto fmt_fset = [](const std::vector<std::tuple<std::string, std::string, int>>& v) {
            std::string s = "[";
            for (std::size_t k = 0; k < v.size(); ++k) {
                s += std::format("{}('{}:{}', {})", k ? ", " : "", std::get<0>(v[k]), std::get<1>(v[k]), std::get<2>(v[k]));
            }
            return s + "]";
        };
        if (want_f != got_file_errors) {
            std::vector<std::tuple<std::string, std::string, int>> missing, extra;
            std::ranges::set_difference(want_f, got_file_errors, std::back_inserter(missing));
            std::ranges::set_difference(got_file_errors, want_f, std::back_inserter(extra));
            if (!missing.empty()) reasons.push_back("missing errors " + fmt_fset(missing));
            if (!extra.empty()) reasons.push_back("unexpected errors " + fmt_fset(extra));
        }
        for (const auto& [rf, w, l] : spec.file_warnings) {
            if (!got_file_warnings.contains({rf, w, l})) {
                reasons.push_back(std::format("missing warning ('{}:{}', {})", rf, w, l));
            }
        }
    } else {
        auto fmt_set = [](const std::vector<std::pair<std::string, int>>& v) {
            std::string s = "[";
            for (std::size_t k = 0; k < v.size(); ++k) s += std::format("{}('{}', {})", k ? ", " : "", v[k].first, v[k].second);
            return s + "]";
        };
        if (want != have) {
            std::vector<std::pair<std::string, int>> missing, extra;
            std::ranges::set_difference(want, have, std::back_inserter(missing));
            std::ranges::set_difference(have, want, std::back_inserter(extra));
            if (!missing.empty()) reasons.push_back("missing errors " + fmt_set(missing));
            if (!extra.empty()) reasons.push_back("unexpected errors " + fmt_set(extra));
        }
        for (const auto& [c, l, col] : spec.errors) {
            if (col != 0 && !got_errors.contains({c, l, col})) reasons.push_back(std::format("error {} @{} expected at column {}", c, l, col));
        }
        for (const auto& w : spec.warnings) {
            if (!got_warnings.contains(w)) reasons.push_back(std::format("missing warning ('{}', {})", w.first, w.second));
        }
    }

    if (needs_run && have.empty()) {
        auto r = run_program(opt, work.path, name, spec, is_test, ev.tsan_skipped);
        if (r.timed_out) {
            reasons.push_back("program timed out");
            return ev;
        }
        const std::string combined = r.out + r.err;
        std::string lower = combined;
        std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (spec.panic) {
            const auto& [sub, line] = *spec.panic;
            if (r.exit_code == 0 || !lower.contains("panic") || !combined.contains(sub)) {
                reasons.push_back(std::format("expected panic containing '{}', got rc={}: '{}'", sub, r.exit_code, tail(combined, 160)));
            } else if (line != 0 && !std::regex_search(combined, std::regex(std::format(R"(\.cf:{}\b)", line)))) {
                reasons.push_back(std::format("panic not attributed to line {}: '{}'", line, tail(combined, 160)));
            }
        } else if (spec.exit_signal) {
#if defined(_WIN32)
            if (r.exit_code == 0) reasons.push_back("expected non-zero signal exit, got 0");
#else
            if (r.exit_code < 128) reasons.push_back(std::format("expected signal exit (>=128), got {}", r.exit_code));
#endif
        } else if (spec.exit) {
            if (r.exit_code != *spec.exit) reasons.push_back(std::format("expected exit {}, got {}", *spec.exit, r.exit_code));
        } else if (r.exit_code != 0) {
            reasons.push_back(std::format("program exited {}: '{}'", r.exit_code, tail(combined, 200)));
        }
        if (spec.stdout_lines) {
            auto rstrip = [](std::string s) {
                while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
                return s;
            };
            std::string body = r.out;
            while (!body.empty() && (body.back() == '\n' || body.back() == '\r')) body.pop_back();
            std::vector<std::string> got;
            for (auto& l : split_lines(body)) got.push_back(rstrip(l));
            std::vector<std::string> want_out;
            for (const auto& l : *spec.stdout_lines) want_out.push_back(rstrip(l));
            auto join = [](const std::vector<std::string>& v, std::size_t from) {
                std::string s = "[";
                for (std::size_t k = from; k < v.size(); ++k) s += std::format("{}'{}'", k > from ? ", " : "", v[k]);
                return s + "]";
            };
            if (is_test) {
                // Test-runner framing lines are allowed around the expected lines (subsequence match).
                std::size_t g = 0;
                bool ok = true;
                for (const auto& w : want_out) {
                    while (g < got.size() && got[g] != w) ++g;
                    if (g == got.size()) { ok = false; break; }
                    ++g;
                }
                if (!ok) reasons.push_back(std::format("test output missing {}; got {}", join(want_out, 0), join(got, got.size() > 8 ? got.size() - 8 : 0)));
            } else if (got != want_out) {
                reasons.push_back(std::format("stdout mismatch: want {}, got {}", join(want_out, 0), join(got, 0)));
            }
        }
        if (has_flag(spec.flags, "--sanitize=thread") && combined.contains("WARNING: ThreadSanitizer"))
            reasons.push_back("ThreadSanitizer reported a data race");
        if (has_flag(spec.flags, "--sanitize") && (combined.contains("AddressSanitizer") || combined.contains("runtime error:")))
            reasons.push_back("ASan/UBSan reported an error");
    }
    ev.met = reasons.empty();
    return ev;
}

enum class Outcome { Pass, Fail, Xfail, Xpass, Skip };

[[nodiscard]] inline auto outcome_name(Outcome o) -> std::string_view {
    switch (o) {
        case Outcome::Pass: return "PASS";
        case Outcome::Fail: return "FAIL";
        case Outcome::Xfail: return "XFAIL";
        case Outcome::Xpass: return "XPASS";
        case Outcome::Skip: return "SKIP";
    }
    return "?";
}

struct FileResult {
    Outcome outcome = Outcome::Fail;
    std::vector<std::string> reasons{};
    std::string xfail{};
    bool tsan_skipped = false;
    std::string skipped_missing_lib{};
};

[[nodiscard]] inline auto classify(const Options& opt, const std::filesystem::path& path) -> FileResult {
    FileResult fr;
    auto ev = evaluate(opt, path);
    if (!ev) {
        fr.reasons.push_back("directive error: " + ev.error().message);
        return fr;
    }
    fr.tsan_skipped = ev->tsan_skipped;
    fr.xfail = ev->spec.xfail;
    if (!ev->skipped_missing_lib.empty()) {
        fr.outcome = Outcome::Skip;
        fr.skipped_missing_lib = ev->skipped_missing_lib;
        fr.reasons.push_back("missing optional library: " + ev->skipped_missing_lib);
        fr.reasons.push_back("install hint: " + remediation_hint_for_lib(ev->skipped_missing_lib));
        return fr;
    }
    if (!fr.xfail.empty()) fr.outcome = ev->met ? Outcome::Xpass : Outcome::Xfail;
    else fr.outcome = ev->met ? Outcome::Pass : Outcome::Fail;
    fr.reasons = std::move(ev->reasons);
    return fr;
}

// Glob with Python fnmatch semantics: `*` and `?` also match `/`; `[abc]`, `[a-z]` and `[!abc]` are classes.
[[nodiscard]] inline auto glob_match(std::string_view pat, std::string_view s) -> bool {
    // Length of the class at pat[p] if it matches c, 0 if it does not match, npos if pat[p] is not a class.
    auto class_at = [&](std::size_t p, char c) -> std::pair<bool, std::size_t> {
        if (pat[p] != '[') return {false, std::string_view::npos};
        std::size_t q = p + 1;
        const bool negate = q < pat.size() && pat[q] == '!';
        if (negate) ++q;
        bool hit = false;
        for (std::size_t first = q; q < pat.size() && (pat[q] != ']' || q == first); ++q) {
            if (q + 2 < pat.size() && pat[q + 1] == '-' && pat[q + 2] != ']') {
                hit |= pat[q] <= c && c <= pat[q + 2];
                q += 2;
            } else {
                hit |= pat[q] == c;
            }
        }
        if (q >= pat.size()) return {false, std::string_view::npos};  // no closing `]`: a literal `[`
        return {hit != negate, q + 1 - p};
    };
    std::size_t p = 0, i = 0, star = std::string_view::npos, mark = 0;
    while (i < s.size()) {
        if (p < pat.size() && pat[p] == '[') {
            if (auto [ok, len] = class_at(p, s[i]); len != std::string_view::npos) {
                if (ok) {
                    p += len, ++i;
                    continue;
                }
                if (star == std::string_view::npos) return false;
                p = star + 1, i = ++mark;
                continue;
            }
        }
        if (p < pat.size() && (pat[p] == '?' || pat[p] == s[i])) {
            ++p, ++i;
        } else if (p < pat.size() && pat[p] == '*') {
            star = p++, mark = i;
        } else if (star != std::string_view::npos) {
            p = star + 1, i = ++mark;
        } else {
            return false;
        }
    }
    while (p < pat.size() && pat[p] == '*') ++p;
    return p == pat.size();
}

// Runs the corpus and prints the report; returns the process exit status (1 on any FAIL or XPASS).
inline auto run_corpus(const Options& opt, const std::filesystem::path& corpus, std::string_view filter, std::size_t jobs, bool verbose) -> int {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& e : std::filesystem::recursive_directory_iterator(corpus, ec)) {
        if (!e.is_regular_file() || e.path().extension() != ".cf") continue;
        if (glob_match(filter, e.path().lexically_relative(corpus).generic_string())) files.push_back(e.path());
    }
    std::ranges::sort(files);
    if (files.empty()) {
        std::println(stderr, "no conformance files match '{}'", filter);
        return 1;
    }
    if (files.size() > 4) {
        ScopedTempDir warm_dir("cfire_conf_warm");
        (void)warm_dir.file("warm.cf", "package main\nfn main() {\n    println(\"ok\")\n}\n");
        std::vector<std::string> warm_argv{opt.cfire, "run", "--no-cache"};
        for (const auto& f : opt.run_flags) {
            if (f != "--sanitize=thread") warm_argv.push_back(f);
        }
        warm_argv.push_back("warm.cf");
        (void)run_process(warm_argv, warm_dir.path, opt.timeout);
    }
    std::vector<FileResult> results(files.size());
    parallel_for_each_index(files.size(), jobs, [&](std::size_t i) { results[i] = classify(opt, files[i]); });

    int counts[5]{};
    std::size_t tsan_skipped = 0;
    for (std::size_t i = 0; i < files.size(); ++i) {
        const auto& r = results[i];
        counts[static_cast<int>(r.outcome)]++;
        tsan_skipped += r.tsan_skipped;
        const std::string rel = files[i].lexically_relative(corpus).generic_string();
        const std::string tag = r.xfail.empty() ? "" : std::format("  [{}]", r.xfail.substr(0, r.xfail.find(' ')));
        if (r.outcome == Outcome::Fail || r.outcome == Outcome::Xpass || r.outcome == Outcome::Skip || verbose) {
            std::println("{:<6} {}{}", outcome_name(r.outcome), rel, tag);
            if (r.outcome == Outcome::Xpass) std::println("         hole is fixed: remove the `expect: xfail {}` line", r.xfail);
            else
                for (const auto& why : r.reasons) std::println("         - {}", why);
        }
    }
    std::println("Conformance: {} passed, {} skipped, {} xfail (tracked holes), {} xpass, {} failed (total {})",
                 counts[0], counts[4], counts[2], counts[3], counts[1], files.size());
    if (tsan_skipped > 0)
        std::println("note: {} file(s) asked for ThreadSanitizer, which this platform lacks; they ran unsanitized", tsan_skipped);
    if (opt.strict && counts[4] > 0) {
        std::println(stderr, "error: --strict enabled and {} conformance test(s) were skipped due to missing optional libraries", counts[4]);
        return 1;
    }
    return (counts[1] > 0 || counts[3] > 0) ? 1 : 0;
}

} // namespace conformance
