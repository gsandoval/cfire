#pragma once
// src/clang_bridge.hpp — C++23 Clang AST Header Ingestor, ParmVarDecl Extractor & Persistent Cache

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#if defined(_WIN32)
#include <process.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#endif

#include "../runtime/cfire_prelude.hpp"
#include "build/toolchain_config.hpp"

#ifndef CFIRE_INFRA_INLINE
#ifdef CFIRE_SPLIT_TUS
#define CFIRE_INFRA_INLINE
#else
#define CFIRE_INFRA_INLINE inline
#endif
#endif

namespace cfire::bridge {

struct CppSymbol {
    std::string name{};
    std::string kind{};
    std::string return_type{};
    std::vector<std::string> param_types{};
    std::vector<CppSymbol> overloads{};
};

struct HeaderIndex {
    std::string header{};
    bool from_cache = false;
    std::unordered_map<std::string, CppSymbol> symbols{};
};

[[nodiscard]] inline auto hash_key(std::string_view sv) noexcept -> std::uint64_t {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (char c : sv) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 0x100000001b3ULL;
    }
    return h;
}

[[nodiscard]] inline auto trim_qual(std::string_view sv) noexcept -> std::string_view {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t')) sv.remove_suffix(1);
    return sv;
}

[[nodiscard]] CFIRE_INFRA_INLINE auto shell_quote(std::string_view arg) -> std::string;

[[nodiscard]] CFIRE_INFRA_INLINE auto normalize_command_for_capture(std::string s) -> std::string;

[[nodiscard]] CFIRE_INFRA_INLINE auto win32_quote_cmd(std::string full_cmd) -> std::string;

[[nodiscard]] CFIRE_INFRA_INLINE auto run_command_capture(const std::string& cmd, int& exit_code, bool merge_stderr = true) -> std::string;

[[nodiscard]] CFIRE_INFRA_INLINE auto run_argv_capture(const std::vector<std::string>& argv, int& exit_code) -> std::string;

[[nodiscard]] CFIRE_INFRA_INLINE auto run_command_capture_with_stdin(
    const std::string& cmd,
    std::string_view stdin_data,
    int& exit_code
) -> std::string;

CFIRE_INFRA_INLINE void parse_fn_qual_type(std::string_view full_sig, CppSymbol& sym);

CFIRE_INFRA_INLINE void extract_clang_json_symbols(std::string_view sv, std::unordered_map<std::string, CppSymbol>& out);

[[nodiscard]] CFIRE_INFRA_INLINE auto next_ast_salt() noexcept -> std::pair<long, std::uint64_t>;

CFIRE_INFRA_INLINE void save_cached_index(const std::filesystem::path& cache_file, const HeaderIndex& idx);

[[nodiscard]] CFIRE_INFRA_INLINE auto load_cached_index(const std::filesystem::path& cache_file, std::string_view header)
    -> std::optional<HeaderIndex>;

[[nodiscard]] CFIRE_INFRA_INLINE auto inspect_header(
    std::string_view header,
    const std::vector<std::string>& cflags,
    const std::filesystem::path& cache_dir = {},
    std::string_view cxx_compiler = ""
) -> std::expected<HeaderIndex, std::string>;

#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_INFRA_IMPL)

[[nodiscard]] CFIRE_INFRA_INLINE auto shell_quote(std::string_view arg) -> std::string {
#if defined(_WIN32)
    std::string out = "\"";
    out.reserve(arg.size() + 8);
    for (char c : arg) {
        if (c == '"') {
            out += "\\\"";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
#else
    std::string out = "'";
    out.reserve(arg.size() + 8);
    for (char c : arg) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('\'');
    return out;
#endif
}

[[nodiscard]] CFIRE_INFRA_INLINE auto normalize_command_for_capture(std::string s) -> std::string {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    if (s.ends_with(" 2>/dev/null")) {
        s.erase(s.size() - 12);
    } else if (s.ends_with(" 2>&1")) {
        s.erase(s.size() - 5);
    }
#if defined(_WIN32)
    for (std::size_t pos = 0; (pos = s.find("/dev/null", pos)) != std::string::npos;) {
        s.replace(pos, 9, "NUL");
        pos += 3;
    }
#endif
    return s;
}

[[nodiscard]] CFIRE_INFRA_INLINE auto win32_quote_cmd(std::string full_cmd) -> std::string {
#if defined(_WIN32)
    if (!full_cmd.empty() && full_cmd.front() == '"') {
        return "\"" + full_cmd + "\"";
    }
#endif
    return full_cmd;
}

[[nodiscard]] CFIRE_INFRA_INLINE auto run_command_capture(const std::string& cmd, int& exit_code, bool merge_stderr) -> std::string {
    std::string out;
    std::string full_cmd = normalize_command_for_capture(cmd);
    if (merge_stderr) {
        full_cmd += " 2>&1";
    }
#if defined(_WIN32)
    std::string popen_cmd = win32_quote_cmd(std::move(full_cmd));
    FILE* pipe = ::_popen(popen_cmd.c_str(), "r");
#else
    FILE* pipe = ::popen(full_cmd.c_str(), "r");
#endif
    if (!pipe) {
        exit_code = -1;
        return "";
    }
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
        out.append(buf);
    }
#if defined(_WIN32)
    exit_code = ::_pclose(pipe);
#else
    int status = ::pclose(pipe);
    exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : status;
#endif
    return out;
}

[[nodiscard]] CFIRE_INFRA_INLINE auto run_argv_capture(const std::vector<std::string>& argv, int& exit_code) -> std::string {
    if (argv.empty()) {
        exit_code = -1;
        return "";
    }
#if defined(_WIN32)
    std::string cmd;
    for (const auto& a : argv) {
        if (!cmd.empty()) cmd += " ";
        cmd += "\"" + a + "\"";
    }
    return run_command_capture(cmd, exit_code);
#else
    int pipefd[2];
    if (::pipe(pipefd) != 0) {
        exit_code = -1;
        return "";
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        exit_code = -1;
        return "";
    }

    if (pid == 0) {
        ::close(pipefd[0]);
        ::dup2(pipefd[1], STDOUT_FILENO);
        ::dup2(pipefd[1], STDERR_FILENO);
        ::close(pipefd[1]);

        std::vector<char*> c_argv;
        c_argv.reserve(argv.size() + 1);
        for (const auto& a : argv) {
            c_argv.push_back(const_cast<char*>(a.c_str()));
        }
        c_argv.push_back(nullptr);

        ::execvp(c_argv[0], c_argv.data());
        ::_exit(127);
    }

    ::close(pipefd[1]);
    std::string out;
    char buf[4096];
    ssize_t n;
    while ((n = ::read(pipefd[0], buf, sizeof(buf))) > 0) {
        out.append(buf, static_cast<std::size_t>(n));
    }
    ::close(pipefd[0]);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            exit_code = -1;
            return out;
        }
    }
    exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : status);
    return out;
#endif
}

[[nodiscard]] CFIRE_INFRA_INLINE auto run_command_capture_with_stdin(
    const std::string& cmd,
    std::string_view stdin_data,
    int& exit_code
) -> std::string {
#if defined(_WIN32)
    (void)cmd;
    (void)stdin_data;
    exit_code = -1;
    return "";
#else
    static const bool kIgnoreSigpipe = []() {
        std::signal(SIGPIPE, SIG_IGN);
        return true;
    }();
    (void)kIgnoreSigpipe;

    int in_pipe[2]{-1, -1};
    int out_pipe[2]{-1, -1};
    if (::pipe(in_pipe) != 0) {
        exit_code = -1;
        return "";
    }
    if (::pipe(out_pipe) != 0) {
        ::close(in_pipe[0]);
        ::close(in_pipe[1]);
        exit_code = -1;
        return "";
    }
#ifdef F_SETNOSIGPIPE
    ::fcntl(in_pipe[1], F_SETNOSIGPIPE, 1);
#endif

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(in_pipe[0]);
        ::close(in_pipe[1]);
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        exit_code = -1;
        return "";
    }

    if (pid == 0) {
        ::close(in_pipe[1]);
        ::close(out_pipe[0]);
        ::dup2(in_pipe[0], STDIN_FILENO);
        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::dup2(out_pipe[1], STDERR_FILENO);
        ::close(in_pipe[0]);
        ::close(out_pipe[1]);

        std::string sh_cmd = normalize_command_for_capture(cmd);
        ::execl("/bin/sh", "sh", "-c", sh_cmd.c_str(), static_cast<char*>(nullptr));
        ::_exit(127);
    }

    ::close(in_pipe[0]);
    ::close(out_pipe[1]);

    std::thread writer([wfd = in_pipe[1], stdin_data]() {
        std::size_t off = 0;
        while (off < stdin_data.size()) {
            ssize_t written = ::write(wfd, stdin_data.data() + off, stdin_data.size() - off);
            if (written > 0) {
                off += static_cast<std::size_t>(written);
            } else if (written < 0 && errno == EINTR) {
                continue;
            } else {
                break;
            }
        }
        ::close(wfd);
    });

    std::string out;
    char buf[4096];
    while (true) {
        ssize_t n = ::read(out_pipe[0], buf, sizeof(buf));
        if (n > 0) {
            out.append(buf, static_cast<std::size_t>(n));
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    ::close(out_pipe[0]);
    writer.join();

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            exit_code = -1;
            return out;
        }
    }
    exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : status);
    return out;
#endif
}

CFIRE_INFRA_INLINE void parse_fn_qual_type(std::string_view full_sig, CppSymbol& sym) {
    std::size_t lp = full_sig.find('(');
    std::size_t rp = full_sig.rfind(')');
    if (lp == std::string_view::npos || rp == std::string_view::npos || rp <= lp) {
        sym.return_type = std::string(trim_qual(full_sig));
        return;
    }
    sym.return_type = std::string(trim_qual(full_sig.substr(0, lp)));
    std::string_view params_sv = trim_qual(full_sig.substr(lp + 1, rp - lp - 1));
    if (params_sv.empty() || params_sv == "void") {
        return;
    }
    int depth = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i < params_sv.size(); ++i) {
        char c = params_sv[i];
        if (c == '<' || c == '(') ++depth;
        else if (c == '>' || c == ')') --depth;
        else if (c == ',' && depth == 0) {
            auto pt = trim_qual(params_sv.substr(start, i - start));
            if (!pt.empty()) sym.param_types.emplace_back(pt);
            start = i + 1;
        }
    }
    auto last_pt = trim_qual(params_sv.substr(start));
    if (!last_pt.empty()) sym.param_types.emplace_back(last_pt);
}

class ClangAstParser {
public:
    explicit ClangAstParser(std::string_view json) : json_(json) {}

    void parse(std::unordered_map<std::string, CppSymbol>& out) {
        std::size_t root_brace = json_.find('{');
        if (root_brace != std::string_view::npos) {
            pos_ = root_brace;
            parse_node(out);
        } else if (!json_.empty()) {
            mark_malformed();
        }
    }

    [[nodiscard]] auto malformed() const noexcept -> bool { return malformed_; }
    [[nodiscard]] auto error_pos() const noexcept -> std::size_t { return error_pos_; }

private:
    std::string_view json_;
    std::size_t pos_ = 0;
    std::vector<std::string> scope_stack_;
    int template_depth_ = 0;
    bool malformed_ = false;
    std::size_t error_pos_ = 0;

    void mark_malformed() noexcept {
        if (!malformed_) {
            malformed_ = true;
            error_pos_ = pos_;
            pos_ = json_.size();
        }
    }

    void skip_ws() {
        while (pos_ < json_.size()) {
            char c = json_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    auto parse_string() -> std::string_view {
        skip_ws();
        if (pos_ >= json_.size() || json_[pos_] != '"') {
            mark_malformed();
            return "";
        }
        ++pos_;
        std::size_t start = pos_;
        while (pos_ < json_.size()) {
            if (json_[pos_] == '\\') {
                if (pos_ + 1 >= json_.size()) {
                    mark_malformed();
                    return "";
                }
                pos_ += 2;
                continue;
            }
            if (json_[pos_] == '"') {
                std::string_view s = json_.substr(start, pos_ - start);
                ++pos_;
                return s;
            }
            ++pos_;
        }
        mark_malformed();
        return "";
    }

    void skip_value() {
        skip_ws();
        if (pos_ >= json_.size()) {
            mark_malformed();
            return;
        }
        char c = json_[pos_];
        if (c == '"') {
            (void)parse_string();
        } else if (c == '{') {
            ++pos_;
            int depth = 1;
            while (pos_ < json_.size() && depth > 0 && !malformed_) {
                if (json_[pos_] == '"') {
                    (void)parse_string();
                } else if (json_[pos_] == '{') {
                    ++depth;
                    ++pos_;
                } else if (json_[pos_] == '}') {
                    --depth;
                    ++pos_;
                } else {
                    ++pos_;
                }
            }
            if (depth != 0) mark_malformed();
        } else if (c == '[') {
            ++pos_;
            int depth = 1;
            while (pos_ < json_.size() && depth > 0 && !malformed_) {
                if (json_[pos_] == '"') {
                    (void)parse_string();
                } else if (json_[pos_] == '[') {
                    ++depth;
                    ++pos_;
                } else if (json_[pos_] == ']') {
                    --depth;
                    ++pos_;
                } else {
                    ++pos_;
                }
            }
            if (depth != 0) mark_malformed();
        } else if (c == ',' || c == '}' || c == ']' || c == ':') {
            mark_malformed();
        } else {
            std::size_t start = pos_;
            while (pos_ < json_.size()) {
                char ch = json_[pos_];
                if (ch == ',' || ch == '}' || ch == ']' || ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
                    break;
                }
                ++pos_;
            }
            if (pos_ == start) mark_malformed();
        }
    }

    void emit_symbol_if_decl(
        std::string_view kind,
        std::string_view name,
        std::string_view qual_type,
        std::unordered_map<std::string, CppSymbol>& out
    ) const {
        if (name.empty() || name.starts_with("__")) return;
        if (kind == "FunctionDecl") {
            if (template_depth_ > 0) return;
            CppSymbol sym;
            sym.name = std::string(name);
            sym.kind = "FunctionDecl";
            parse_fn_qual_type(qual_type, sym);

            std::string qname;
            for (const auto& s : scope_stack_) {
                qname += s + "::";
            }
            qname += sym.name;

            auto it = out.find(qname);
            if (it == out.end()) {
                out[qname] = sym;
            } else {
                it->second.overloads.push_back(sym);
            }
        } else if (kind == "TypedefDecl" || kind == "RecordDecl" || kind == "CXXRecordDecl" ||
                   kind == "VarDecl" || kind == "EnumConstantDecl" || kind == "EnumDecl") {
            std::string qname;
            for (const auto& s : scope_stack_) {
                qname += s + "::";
            }
            qname += std::string(name);
            out.try_emplace(qname, CppSymbol{std::string(name), std::string(kind), "", {}, {}});
            if (!scope_stack_.empty()) {
                out.try_emplace(std::string(name), CppSymbol{std::string(name), std::string(kind), "", {}, {}});
            }
        }
    }

    void parse_inner_elements(std::unordered_map<std::string, CppSymbol>& out) {
        while (pos_ < json_.size() && !malformed_) {
            skip_ws();
            if (pos_ >= json_.size()) {
                mark_malformed();
                return;
            }
            if (json_[pos_] == ']') {
                ++pos_;
                return;
            }
            std::size_t elem_start = pos_;
            if (json_[pos_] == '{') {
                parse_node(out);
            } else {
                skip_value();
            }
            if (malformed_ || pos_ == elem_start) {
                mark_malformed();
                return;
            }
            skip_ws();
            if (pos_ >= json_.size()) {
                mark_malformed();
                return;
            }
            if (json_[pos_] == ',') {
                ++pos_;
            } else if (json_[pos_] != ']') {
                mark_malformed();
                return;
            }
        }
        if (pos_ >= json_.size() && !malformed_) mark_malformed();
    }

    void parse_node(std::unordered_map<std::string, CppSymbol>& out) {
        skip_ws();
        if (pos_ >= json_.size() || json_[pos_] != '{') {
            mark_malformed();
            return;
        }
        ++pos_; // consume '{'

        std::string_view kind;
        std::string_view name;
        std::string_view qual_type;
        std::size_t deferred_inner_start = std::string_view::npos;
        bool inner_parsed = false;

        while (pos_ < json_.size() && !malformed_) {
            skip_ws();
            if (pos_ >= json_.size()) {
                mark_malformed();
                return;
            }
            if (json_[pos_] == '}') {
                ++pos_;
                break;
            }
            if (json_[pos_] != '"') {
                mark_malformed();
                return;
            }

            std::size_t field_start = pos_;
            auto key = parse_string();
            if (malformed_) return;
            skip_ws();
            if (pos_ >= json_.size() || json_[pos_] != ':') {
                mark_malformed();
                return;
            }
            ++pos_;
            skip_ws();

            if (key == "kind") {
                kind = parse_string();
            } else if (key == "name") {
                name = parse_string();
            } else if (key == "type") {
                if (pos_ < json_.size() && json_[pos_] == '{') {
                    ++pos_;
                    while (pos_ < json_.size() && !malformed_) {
                        skip_ws();
                        if (pos_ >= json_.size()) { mark_malformed(); return; }
                        if (json_[pos_] == '}') { ++pos_; break; }
                        if (json_[pos_] != '"') { mark_malformed(); return; }
                        auto sub_key = parse_string();
                        if (malformed_) return;
                        skip_ws();
                        if (pos_ >= json_.size() || json_[pos_] != ':') { mark_malformed(); return; }
                        ++pos_;
                        skip_ws();
                        if (sub_key == "qualType") {
                            qual_type = parse_string();
                        } else {
                            std::size_t before = pos_;
                            skip_value();
                            if (malformed_ || pos_ == before) { mark_malformed(); return; }
                        }
                        if (malformed_) return;
                        skip_ws();
                        if (pos_ >= json_.size()) { mark_malformed(); return; }
                        if (json_[pos_] == ',') ++pos_;
                        else if (json_[pos_] != '}') { mark_malformed(); return; }
                    }
                } else {
                    std::size_t before = pos_;
                    skip_value();
                    if (malformed_ || pos_ == before) { mark_malformed(); return; }
                }
            } else if (key == "inner") {
                if (pos_ < json_.size() && json_[pos_] == '[') {
                    if (!kind.empty()) {
                        const bool is_scope = (kind == "NamespaceDecl" || kind == "CXXRecordDecl" || kind == "RecordDecl");
                        const bool pushed_scope = is_scope && !name.empty() && !name.starts_with("__");
                        const bool is_tmpl = (kind == "FunctionTemplateDecl" || kind == "ClassTemplateDecl");
                        if (pushed_scope) scope_stack_.emplace_back(name);
                        if (is_tmpl) ++template_depth_;
                        ++pos_; // consume '['
                        parse_inner_elements(out);
                        if (is_tmpl) --template_depth_;
                        if (pushed_scope) scope_stack_.pop_back();
                        inner_parsed = true;
                    } else {
                        deferred_inner_start = pos_;
                        std::size_t before = pos_;
                        skip_value();
                        if (malformed_ || pos_ == before) { mark_malformed(); return; }
                    }
                } else {
                    std::size_t before = pos_;
                    skip_value();
                    if (malformed_ || pos_ == before) { mark_malformed(); return; }
                }
            } else {
                std::size_t before = pos_;
                skip_value();
                if (malformed_ || pos_ == before) { mark_malformed(); return; }
            }

            if (malformed_ || pos_ == field_start) {
                mark_malformed();
                return;
            }
            skip_ws();
            if (pos_ >= json_.size()) {
                mark_malformed();
                return;
            }
            if (json_[pos_] == ',') {
                ++pos_;
            } else if (json_[pos_] != '}') {
                mark_malformed();
                return;
            }
        }

        if (malformed_) return;

        emit_symbol_if_decl(kind, name, qual_type, out);

        if (!inner_parsed && deferred_inner_start != std::string_view::npos) {
            const bool is_scope = (kind == "NamespaceDecl" || kind == "CXXRecordDecl" || kind == "RecordDecl");
            const bool pushed_scope = is_scope && !name.empty() && !name.starts_with("__");
            const bool is_tmpl = (kind == "FunctionTemplateDecl" || kind == "ClassTemplateDecl");
            if (pushed_scope) scope_stack_.emplace_back(name);
            if (is_tmpl) ++template_depth_;
            std::size_t saved_pos = pos_;
            pos_ = deferred_inner_start + 1;
            parse_inner_elements(out);
            if (!malformed_) pos_ = saved_pos;
            if (is_tmpl) --template_depth_;
            if (pushed_scope) scope_stack_.pop_back();
        }
    }
};

CFIRE_INFRA_INLINE void extract_clang_json_symbols(std::string_view sv, std::unordered_map<std::string, CppSymbol>& out) {
    ClangAstParser parser(sv);
    parser.parse(out);
}

[[nodiscard]] CFIRE_INFRA_INLINE auto next_ast_salt() noexcept -> std::pair<long, std::uint64_t> {
    static std::atomic<std::uint64_t> s_seq{0};
#if defined(_WIN32)
    long pid = static_cast<long>(::_getpid());
#else
    long pid = static_cast<long>(::getpid());
#endif
    return {pid, ++s_seq};
}

CFIRE_INFRA_INLINE void save_cached_index(const std::filesystem::path& cache_file, const HeaderIndex& idx) {
    std::error_code ec;
    std::filesystem::create_directories(cache_file.parent_path(), ec);
    auto [pid, seq] = next_ast_salt();
    auto tmp_file = std::filesystem::path(cache_file.string() + std::format(".tmp.{}.{}", pid, seq));
    {
        std::ofstream out(tmp_file, std::ios::binary | std::ios::trunc);
        for (const auto& [key, sym] : idx.symbols) {
            out << key << "\t" << sym.name << "\t" << sym.kind << "\t" << sym.return_type;
            for (const auto& pt : sym.param_types) {
                out << "\t" << pt;
            }
            out << "\n";
            for (const auto& ov : sym.overloads) {
                out << key << "\t" << ov.name << "\t" << ov.kind << "\t" << ov.return_type;
                for (const auto& pt : ov.param_types) {
                    out << "\t" << pt;
                }
                out << "\n";
            }
        }
    }
    std::filesystem::rename(tmp_file, cache_file, ec);
    if (ec) {
        std::filesystem::copy_file(tmp_file, cache_file, std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(tmp_file, ec);
    }
}

[[nodiscard]] CFIRE_INFRA_INLINE auto load_cached_index(const std::filesystem::path& cache_file, std::string_view header)
    -> std::optional<HeaderIndex> {
    if (!std::filesystem::exists(cache_file)) return std::nullopt;
    std::ifstream in(cache_file, std::ios::in | std::ios::binary);
    if (!in.is_open()) return std::nullopt;

    HeaderIndex idx;
    idx.header = std::string(header);
    idx.from_cache = true;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::vector<std::string> cols;
        std::size_t start = 0;
        for (;;) {
            std::size_t tab = line.find('\t', start);
            if (tab == std::string::npos) {
                cols.push_back(line.substr(start));
                break;
            }
            cols.push_back(line.substr(start, tab - start));
            start = tab + 1;
        }
        if (cols.size() >= 4) {
            std::string key = cols[0];
            CppSymbol sym;
            sym.name = cols[1];
            sym.kind = cols[2];
            sym.return_type = cols[3];
            for (std::size_t i = 4; i < cols.size(); ++i) {
                sym.param_types.push_back(cols[i]);
            }
            auto it = idx.symbols.find(key);
            if (it == idx.symbols.end()) {
                idx.symbols[key] = std::move(sym);
            } else {
                it->second.overloads.push_back(std::move(sym));
            }
        }
    }
    return idx;
}

[[nodiscard]] CFIRE_INFRA_INLINE auto inspect_header(
    std::string_view header,
    const std::vector<std::string>& cflags,
    const std::filesystem::path& cache_dir,
    std::string_view cxx_compiler) -> std::expected<HeaderIndex, std::string> {
    std::string clean_hdr(header);
    while (!clean_hdr.empty() && (clean_hdr.front() == '<' || clean_hdr.front() == '"')) clean_hdr.erase(clean_hdr.begin());
    while (!clean_hdr.empty() && (clean_hdr.back() == '>' || clean_hdr.back() == '"')) clean_hdr.pop_back();

    std::string clang_bin = !cxx_compiler.empty() ? std::string(cxx_compiler) : cfire::build::resolve_cxx_compiler();
    if (clang_bin.empty()) {
        clang_bin = "clang++";
    }
    if (clang_bin.find("g++") != std::string::npos && clang_bin.find("clang") == std::string::npos) {
        auto discovered_clang = cfire::build::find_executable_in_path("clang++");
        clang_bin = discovered_clang.empty() ? std::string("clang++") : discovered_clang.string();
    }

    std::string key_str = "v5:" + clean_hdr + "|cc:" + cfire::build::compiler_binary_fingerprint(clang_bin);
    for (const auto& f : cflags) {
        key_str += "|" + f;
        // Include mtime if local -I directory contains this header
        if (f.starts_with("-I")) {
            std::filesystem::path candidate = std::filesystem::path(f.substr(2)) / clean_hdr;
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec)) {
                auto wt = std::filesystem::last_write_time(candidate, ec).time_since_epoch().count();
                key_str += std::format("|{}", wt);
            }
        }
    }
    std::filesystem::path cache_file;
    if (!cache_dir.empty()) {
        cache_file = cache_dir / "ast" / std::format("{:016x}.idx", hash_key(key_str));
        if (auto cached = load_cached_index(cache_file, header)) {
            return *cached;
        }
    }

    cfire::detail::SecureTempDir ast_tmp("cfire_ast");
    if (!ast_tmp.ok()) {
        return std::unexpected("failed to create private temporary directory for Clang AST inspection");
    }
    auto tu_path = ast_tmp.file("tu.cpp");
    auto stderr_path = ast_tmp.file("stderr.log");

    {
        std::ofstream out(tu_path);
        out << std::format("#include <{}>\n", clean_hdr);
    }

    auto build_ast_cmd = [&](std::string_view bin) {
        std::string c = std::string(bin) + " -std=c++2b -Xclang -ast-dump=json -fsyntax-only";
#if defined(__linux__)
        c += " -Wno-builtin-macro-redefined -D__cpp_concepts=202002L";
#endif
        for (const auto& f : cflags) {
            c += " " + shell_quote(f);
        }
        c += " " + shell_quote(tu_path.string());
        c += " 2>" + shell_quote(stderr_path.string());
        return c;
    };

    int code = 0;
    std::string json_dump = run_command_capture(build_ast_cmd(clang_bin), code, false);
    if (code != 0 && clang_bin != "clang++") {
        int fallback_code = 0;
        std::string fallback_dump = run_command_capture(build_ast_cmd("clang++"), fallback_code, false);
        if (fallback_code == 0) {
            code = 0;
            json_dump = std::move(fallback_dump);
        }
    }

    auto read_stderr_log = [&]() -> std::string {
        std::ifstream err_in(stderr_path, std::ios::in | std::ios::binary);
        if (!err_in.is_open()) return "";
        std::ostringstream ss;
        ss << err_in.rdbuf();
        std::string s = ss.str();
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
        return s;
    };

    if (code != 0) {
        std::string filtered = read_stderr_log();
        if (filtered.empty()) {
            std::size_t pos = 0;
            while (pos < json_dump.size()) {
                std::size_t nl = json_dump.find('\n', pos);
                std::string_view line = (nl == std::string::npos)
                    ? std::string_view(json_dump).substr(pos)
                    : std::string_view(json_dump).substr(pos, nl - pos);
                pos = (nl == std::string::npos) ? json_dump.size() : nl + 1;
                std::string_view trimmed = line;
                while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t' || trimmed.front() == '\r')) {
                    trimmed.remove_prefix(1);
                }
                if (trimmed.empty()) continue;
                char first = trimmed.front();
                if (first == '{' || first == '[' || first == '}' || first == ']' || first == '"') {
                    continue;
                }
                if (!filtered.empty()) filtered += '\n';
                filtered += line;
            }
            if (filtered.empty()) filtered = json_dump;
        }
        return std::unexpected(std::format("clang AST ingestion failed for {}: {}", header, filtered));
    }

    HeaderIndex idx;
    idx.header = std::string(header);
    idx.from_cache = false;
    ClangAstParser parser(json_dump);
    parser.parse(idx.symbols);
    if (parser.malformed()) {
        std::string err_note = read_stderr_log();
        return std::unexpected(std::format(
            "clang AST ingestion produced malformed JSON for {} at byte offset {}{}",
            header,
            parser.error_pos(),
            err_note.empty() ? std::string("") : std::format(" ({})", err_note)));
    }

    if (!cache_file.empty()) {
        save_cached_index(cache_file, idx);
    }
    return idx;
}

#endif // !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_INFRA_IMPL)

} // namespace cfire::bridge

#undef CFIRE_INFRA_INLINE
