#pragma once
// tests/conformance_runner.hpp — Language conformance corpus runner (CF-161; C++ since CF-186).
//
// Each tests/conformance/**/*.cf file pins one language expectation with leading `//` directives:
//
//     // expect: stdout              exact program stdout follows, one `// | <line>` per line
//     // | hello
//     // expect: error cfire/CODE @L[:C]     (repeatable) the exact set of errors `cfire check` must report
//     // expect: warning cfire/CODE @L       (repeatable) warnings that must be present
//     // expect: panic "<substring>" [@L]    program must panic (non-zero exit, stderr contains `panic`)
//     // expect: exit N                      program exit status (default 0, or non-zero with `panic`)
//     // expect: xfail CF-xxx <reason>       ideal expectations above are NOT met yet (tracked hole)
//     <code> //~ error cfire/CODE            inline form, preferred: expected error on this very line
//     <code> //~ warning cfire/CODE          inline expected warning
//     <code> //~ panic "<substring>"         inline expected panic attributed to this line
//     // flags: --sanitize | --sanitize=thread   extra run flags (thread falls back to emit-cpp + TSan)
//
// Files named *_test.cf run with `cfire test` instead of `cfire run`.
// Outcomes: PASS, FAIL, XFAIL (known hole, expected), XPASS (hole now fixed: drop the xfail line).
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

struct Spec {
    std::optional<std::vector<std::string>> stdout_lines{};
    std::set<std::tuple<std::string, int, int>> errors{};  // (code, line, col or 0 = any)
    std::set<std::pair<std::string, int>> warnings{};
    std::optional<std::pair<std::string, int>> panic{};    // (substring, line or 0 = any)
    std::optional<int> exit{};
    std::string xfail{};
    std::vector<std::string> flags{};
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

[[nodiscard]] inline auto parse_spec(std::string_view text) -> std::expected<Spec, DirectiveError> {
    static const std::regex kDiag(R"((cfire/[A-Z0-9-]+)\s+@(\d+)(?::(\d+))?)");
    static const std::regex kPanic(R"rx("([^"]*)"(?:\s+@(\d+))?)rx");
    static const std::regex kInlineDiag(R"(//~\s*(error|warning)\s+(cfire/[A-Z0-9-]+)\s*$)");
    static const std::regex kInlinePanic(R"rx(//~\s*panic\s+"([^"]*)"\s*$)rx");
    static const std::regex kCfId(R"(CF-\d+.*)");
    auto bad = [](std::string m) { return std::unexpected(DirectiveError{std::move(m)}); };

    Spec spec;
    const auto lines = split_lines(text);
    std::size_t i = 0;
    while (i < lines.size() && lines[i].starts_with("//")) {
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
                try {
                    spec.exit = std::stoi(arg);
                } catch (...) {
                    return bad(std::format("bad exit directive: '{}'", body));
                }
            } else if (kind == "xfail") {
                if (!std::regex_match(arg, kCfId)) return bad(std::format("xfail must cite a backlog ID: '{}'", body));
                spec.xfail = arg;
            } else {
                return bad(std::format("unknown expectation '{}'", kind));
            }
        } else if (body.starts_with("flags:")) {
            spec.flags = split_ws(std::string_view(body).substr(6));
        }
        ++i;
    }
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
    if (!spec.stdout_lines && spec.errors.empty() && !spec.panic && !spec.exit && spec.warnings.empty()) {
        return bad("file declares no expectation");
    }
    return spec;
}

// Q47 (CF-184): clang's Windows targets have no ThreadSanitizer. Files that ask for it still run there, unsanitized,
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
};

struct Evaluation {
    Spec spec{};
    bool met = false;
    std::vector<std::string> reasons{};
    bool tsan_skipped = false;
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
        auto r = run_process({opt.cfire, "run", "--sanitize=thread", name}, work, opt.timeout);
        if (!r.err.contains("unrecognized flag") && !r.out.contains("unrecognized flag")) return r;
        // Fallback until CF-164 adds a native mode.
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
    std::vector<std::string> argv{opt.cfire, is_test ? "test" : "run"};
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
    for (const auto& f : opt.run_flags) {
        if (!has_flag(spec.flags, f)) spec.flags.push_back(f);
    }
    auto& reasons = ev.reasons;
    const std::string fname = path.filename().string();
    const bool is_test = fname.ends_with("_test.cf");
    ScopedTempDir work("cfire_conf");
    const std::string name = is_test ? "main_test.cf" : "main.cf";
    std::error_code ec;
    std::filesystem::copy_file(path, work.path / name, std::filesystem::copy_options::overwrite_existing, ec);

    auto chk = run_process({opt.cfire, "check", "--json", name}, work.path, opt.timeout);
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
    std::set<std::tuple<std::string, int, int>> got_errors;
    std::set<std::pair<std::string, int>> got_warnings;
    if (auto diags = report->get("diagnostics"); diags && diags->is_array()) {
        for (const auto& d : diags->as_array()) {
            auto str_of = [&](std::string_view k) { auto v = d.get(k); return v && v->is_string() ? std::string(v->as_str()) : std::string(); };
            auto int_of = [&](std::string_view k) { auto v = d.get(k); return v && v->is_number() ? static_cast<int>(v->as_int()) : 0; };
            const auto sev = str_of("severity");
            if (sev == "error") got_errors.emplace(str_of("code"), int_of("line"), int_of("col"));
            else if (sev == "warning") got_warnings.emplace(str_of("code"), int_of("line"));
        }
    }

    // ICE gate (CF-148): `cfire/TYPE-ERROR` means clang rejected generated C++ for a program Sema accepted. It is
    // never an acceptable expectation; Sema must report a cfire diagnostic instead.
    for (const auto& [c, l, col] : spec.errors) {
        if (c == "cfire/TYPE-ERROR") return std::unexpected(DirectiveError{"cfire/TYPE-ERROR (a clang backend error) can't be an expected diagnostic"});
    }
    for (const auto& [c, l, col] : got_errors) {
        if (c == "cfire/TYPE-ERROR")
            reasons.push_back(std::format("internal compiler error: clang rejected generated C++ at line {} for a program Sema accepted", l));
    }
    std::erase_if(got_errors, [](const auto& e) { return std::get<0>(e) == "cfire/TYPE-ERROR"; });
    std::set<std::pair<std::string, int>> want, have;
    for (const auto& [c, l, col] : spec.errors) want.emplace(c, l);
    for (const auto& [c, l, col] : got_errors) have.emplace(c, l);
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

    const bool needs_run = spec.stdout_lines || spec.panic || spec.exit;
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

enum class Outcome { Pass, Fail, Xfail, Xpass };

[[nodiscard]] inline auto outcome_name(Outcome o) -> std::string_view {
    switch (o) {
        case Outcome::Pass: return "PASS";
        case Outcome::Fail: return "FAIL";
        case Outcome::Xfail: return "XFAIL";
        case Outcome::Xpass: return "XPASS";
    }
    return "?";
}

struct FileResult {
    Outcome outcome = Outcome::Fail;
    std::vector<std::string> reasons{};
    std::string xfail{};
    bool tsan_skipped = false;
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
    std::vector<FileResult> results(files.size());
    parallel_for_each_index(files.size(), jobs, [&](std::size_t i) { results[i] = classify(opt, files[i]); });

    int counts[4]{};
    std::size_t tsan_skipped = 0;
    for (std::size_t i = 0; i < files.size(); ++i) {
        const auto& r = results[i];
        counts[static_cast<int>(r.outcome)]++;
        tsan_skipped += r.tsan_skipped;
        const std::string rel = files[i].lexically_relative(corpus).generic_string();
        const std::string tag = r.xfail.empty() ? "" : std::format("  [{}]", r.xfail.substr(0, r.xfail.find(' ')));
        if (r.outcome == Outcome::Fail || r.outcome == Outcome::Xpass || verbose) {
            std::println("{:<6} {}{}", outcome_name(r.outcome), rel, tag);
            if (r.outcome == Outcome::Xpass) std::println("         hole is fixed: remove the `expect: xfail {}` line", r.xfail);
            else
                for (const auto& why : r.reasons) std::println("         - {}", why);
        }
    }
    std::println("Conformance: {} passed, {} xfail (tracked holes), {} xpass, {} failed (total {})", counts[0], counts[2], counts[3], counts[1],
                 files.size());
    if (tsan_skipped > 0)
        std::println("note: {} file(s) asked for ThreadSanitizer, which this platform lacks; they ran unsanitized (Q47)", tsan_skipped);
    return (counts[1] > 0 || counts[3] > 0) ? 1 : 0;
}

} // namespace conformance
