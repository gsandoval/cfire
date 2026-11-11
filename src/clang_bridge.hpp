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
#include <sys/wait.h>
#include <unistd.h>
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

[[nodiscard]] inline auto shell_quote(std::string_view arg) -> std::string {
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

[[nodiscard]] inline auto normalize_command_for_capture(std::string s) -> std::string {
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

[[nodiscard]] inline auto win32_quote_cmd(std::string full_cmd) -> std::string {
#if defined(_WIN32)
    if (!full_cmd.empty() && full_cmd.front() == '"') {
        return "\"" + full_cmd + "\"";
    }
#endif
    return full_cmd;
}

[[nodiscard]] inline auto run_command_capture(const std::string& cmd, int& exit_code) -> std::string {
    std::string out;
    std::string full_cmd = normalize_command_for_capture(cmd) + " 2>&1";
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

[[nodiscard]] inline auto run_argv_capture(const std::vector<std::string>& argv, int& exit_code) -> std::string {
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

inline void parse_fn_qual_type(std::string_view full_sig, CppSymbol& sym) {
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
        }
    }

private:
    std::string_view json_;
    std::size_t pos_ = 0;
    std::vector<std::string> scope_stack_;

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
        if (pos_ >= json_.size() || json_[pos_] != '"') return "";
        ++pos_;
        std::size_t start = pos_;
        while (pos_ < json_.size()) {
            if (json_[pos_] == '\\') {
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
        return "";
    }

    void skip_value() {
        skip_ws();
        if (pos_ >= json_.size()) return;
        char c = json_[pos_];
        if (c == '"') {
            (void)parse_string();
        } else if (c == '{') {
            ++pos_;
            int depth = 1;
            while (pos_ < json_.size() && depth > 0) {
                if (json_[pos_] == '"') (void)parse_string();
                else if (json_[pos_] == '{') { ++depth; ++pos_; }
                else if (json_[pos_] == '}') { --depth; ++pos_; }
                else ++pos_;
            }
        } else if (c == '[') {
            ++pos_;
            int depth = 1;
            while (pos_ < json_.size() && depth > 0) {
                if (json_[pos_] == '"') (void)parse_string();
                else if (json_[pos_] == '[') { ++depth; ++pos_; }
                else if (json_[pos_] == ']') { --depth; ++pos_; }
                else ++pos_;
            }
        } else {
            while (pos_ < json_.size() && json_[pos_] != ',' && json_[pos_] != '}' && json_[pos_] != ']') {
                ++pos_;
            }
        }
    }

    void parse_node(std::unordered_map<std::string, CppSymbol>& out) {
        if (pos_ >= json_.size() || json_[pos_] != '{') return;
        ++pos_; // consume '{'

        std::string_view kind;
        std::string_view name;
        std::string_view qual_type;
        std::size_t inner_array_start = std::string_view::npos;

        while (pos_ < json_.size()) {
            skip_ws();
            if (pos_ >= json_.size() || json_[pos_] == '}') {
                if (pos_ < json_.size()) ++pos_;
                break;
            }

            auto key = parse_string();
            skip_ws();
            if (pos_ < json_.size() && json_[pos_] == ':') ++pos_;
            skip_ws();

            if (key == "kind") {
                kind = parse_string();
            } else if (key == "name") {
                name = parse_string();
            } else if (key == "type") {
                if (pos_ < json_.size() && json_[pos_] == '{') {
                    ++pos_;
                    while (pos_ < json_.size() && json_[pos_] != '}') {
                        skip_ws();
                        auto sub_key = parse_string();
                        skip_ws();
                        if (pos_ < json_.size() && json_[pos_] == ':') ++pos_;
                        skip_ws();
                        if (sub_key == "qualType") {
                            qual_type = parse_string();
                        } else {
                            skip_value();
                        }
                        skip_ws();
                        if (pos_ < json_.size() && json_[pos_] == ',') ++pos_;
                    }
                    if (pos_ < json_.size() && json_[pos_] == '}') ++pos_;
                } else {
                    skip_value();
                }
            } else if (key == "inner") {
                skip_ws();
                if (pos_ < json_.size() && json_[pos_] == '[') {
                    inner_array_start = pos_;
                    int depth = 1;
                    ++pos_;
                    while (pos_ < json_.size() && depth > 0) {
                        if (json_[pos_] == '"') (void)parse_string();
                        else if (json_[pos_] == '[') { ++depth; ++pos_; }
                        else if (json_[pos_] == ']') { --depth; ++pos_; }
                        else ++pos_;
                    }
                } else {
                    skip_value();
                }
            } else {
                skip_value();
            }

            skip_ws();
            if (pos_ < json_.size() && json_[pos_] == ',') ++pos_;
        }

        if (kind == "FunctionDecl" && !name.empty() && !name.starts_with("__")) {
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
            if (!scope_stack_.empty()) {
                auto sit = out.find(sym.name);
                if (sit == out.end()) {
                    out[sym.name] = sym;
                } else {
                    sit->second.overloads.push_back(sym);
                }
            }
        } else if (!name.empty() && !name.starts_with("__") &&
                   (kind == "TypedefDecl" || kind == "RecordDecl" || kind == "CXXRecordDecl" ||
                    kind == "VarDecl" || kind == "EnumConstantDecl" || kind == "EnumDecl")) {
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

        bool is_scope = (kind == "NamespaceDecl" || kind == "CXXRecordDecl" || kind == "RecordDecl");
        if (is_scope && !name.empty() && !name.starts_with("__")) {
            scope_stack_.emplace_back(name);
        }

        if (inner_array_start != std::string_view::npos) {
            std::size_t saved_pos = pos_;
            pos_ = inner_array_start + 1;
            while (pos_ < json_.size()) {
                skip_ws();
                if (pos_ >= json_.size() || json_[pos_] == ']') break;
                if (json_[pos_] == '{') {
                    parse_node(out);
                } else {
                    skip_value();
                }
                skip_ws();
                if (pos_ < json_.size() && json_[pos_] == ',') ++pos_;
            }
            pos_ = saved_pos;
        }

        if (is_scope && !name.empty() && !name.starts_with("__")) {
            scope_stack_.pop_back();
        }
    }
};

inline void extract_clang_json_symbols(std::string_view sv, std::unordered_map<std::string, CppSymbol>& out) {
    ClangAstParser parser(sv);
    parser.parse(out);
}

[[nodiscard]] inline auto next_ast_salt() noexcept -> std::pair<long, std::uint64_t> {
    static std::atomic<std::uint64_t> s_seq{0};
#if defined(_WIN32)
    long pid = static_cast<long>(::_getpid());
#else
    long pid = static_cast<long>(::getpid());
#endif
    return {pid, ++s_seq};
}

inline void save_cached_index(const std::filesystem::path& cache_file, const HeaderIndex& idx) {
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

[[nodiscard]] inline auto load_cached_index(const std::filesystem::path& cache_file, std::string_view header)
    -> std::optional<HeaderIndex>
{
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

// Inspects a native C/C++ header via `clang++ -std=c++23 -Xclang -ast-dump=json -fsyntax-only`
// (or loads from `.cfire/cache/ast/<hash>.idx` if already cached).
[[nodiscard]] inline auto inspect_header(
    std::string_view header,
    const std::vector<std::string>& cflags,
    const std::filesystem::path& cache_dir = {},
    std::string_view cxx_compiler = ""
) -> std::expected<HeaderIndex, std::string> {
    std::string clean_hdr(header);
    while (!clean_hdr.empty() && (clean_hdr.front() == '<' || clean_hdr.front() == '"')) clean_hdr.erase(clean_hdr.begin());
    while (!clean_hdr.empty() && (clean_hdr.back() == '>' || clean_hdr.back() == '"')) clean_hdr.pop_back();

    std::string clang_bin = !cxx_compiler.empty() ? std::string(cxx_compiler) : std::string{};
    if (clang_bin.empty()) {
        if (const char* env_cxx = std::getenv("CFIRE_CXX"); env_cxx && *env_cxx) {
            clang_bin = env_cxx;
        } else {
            clang_bin = "clang++";
        }
    }
    if (clang_bin.find("g++") != std::string::npos && clang_bin.find("clang") == std::string::npos) {
        clang_bin = "clang++";
    }

    std::string key_str = "v3:" + clean_hdr + "|cc:" + clang_bin;
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

    auto [pid, seq] = next_ast_salt();
    std::error_code tmp_ec;
    auto tmp_dir = std::filesystem::temp_directory_path(tmp_ec) /
                   std::format("cfire_clang_ast_{}_{}_{}", hash_key(key_str), pid, seq);
    std::filesystem::create_directories(tmp_dir, tmp_ec);
    auto tu_path = tmp_dir / "tu.cpp";

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
        return c;
    };

    int code = 0;
    std::string json_dump = run_command_capture(build_ast_cmd(clang_bin), code);
    if (code != 0 && clang_bin != "clang++") {
        int fallback_code = 0;
        std::string fallback_dump = run_command_capture(build_ast_cmd("clang++"), fallback_code);
        if (fallback_code == 0) {
            code = 0;
            json_dump = std::move(fallback_dump);
        }
    }
    std::filesystem::remove_all(tmp_dir, tmp_ec);

    if (code != 0) {
        std::string filtered;
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
        return std::unexpected(std::format("clang AST ingestion failed for {}: {}", header, filtered));
    }

    HeaderIndex idx;
    idx.header = std::string(header);
    idx.from_cache = false;
    extract_clang_json_symbols(json_dump, idx.symbols);

    if (!cache_file.empty()) {
        save_cached_index(cache_file, idx);
    }
    return idx;
}

} // namespace cfire::bridge
