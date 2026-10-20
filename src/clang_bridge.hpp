#pragma once
// src/clang_bridge.hpp — C++23 Clang AST Header Ingestor, ParmVarDecl Extractor & Persistent Cache

#include <cstdio>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "bootstrap/clang_bridge.generated.hpp"

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

[[nodiscard]] inline auto run_command_capture(const std::string& cmd, int& exit_code) -> std::string {
    std::string out;
    FILE* pipe = popen((cmd + " 2>&1").c_str(), "r");
    if (!pipe) {
        exit_code = -1;
        return "";
    }
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
        out.append(buf);
    }
    int status = pclose(pipe);
    exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : status;
    return out;
}

[[nodiscard]] inline auto hash_key(std::string_view sv) noexcept -> std::uint64_t {
#if !defined(CFIRE_CXX_CLANG_BRIDGE)
    return selfhost_clang_hash_key(sv);
#else
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (char c : sv) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 0x100000001b3ULL;
    }
    return h;
#endif
}

[[nodiscard]] inline auto trim_qual(std::string_view sv) noexcept -> std::string_view {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t')) sv.remove_suffix(1);
    return sv;
}

inline void parse_fn_qual_type(std::string_view full_sig, CppSymbol& sym) {
#if !defined(CFIRE_CXX_CLANG_BRIDGE)
    auto parsed = selfhost_clang_parse_fn_sig(full_sig);
    sym.return_type = std::move(parsed.return_type);
    sym.param_types = std::move(parsed.param_types);
#else
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
#endif
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

inline void save_cached_index(const std::filesystem::path& cache_file, const HeaderIndex& idx) {
    std::filesystem::create_directories(cache_file.parent_path());
    std::ofstream out(cache_file, std::ios::binary | std::ios::trunc);
    for (const auto& [key, sym] : idx.symbols) {
#if !defined(CFIRE_CXX_CLANG_BRIDGE)
        CppCacheEntry entry{.key = key, .name = sym.name, .kind = sym.kind, .return_type = sym.return_type, .param_types = sym.param_types};
        out << selfhost_clang_format_cache_entry(entry);
        for (const auto& ov : sym.overloads) {
            CppCacheEntry ov_entry{.key = key, .name = ov.name, .kind = ov.kind, .return_type = ov.return_type, .param_types = ov.param_types};
            out << selfhost_clang_format_cache_entry(ov_entry);
        }
#else
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
#endif
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
#if !defined(CFIRE_CXX_CLANG_BRIDGE)
        auto entry = selfhost_clang_parse_cache_line(line);
        if (!entry.key.empty()) {
            CppSymbol sym;
            sym.name = std::move(entry.name);
            sym.kind = std::move(entry.kind);
            sym.return_type = std::move(entry.return_type);
            sym.param_types = std::move(entry.param_types);
            auto it = idx.symbols.find(entry.key);
            if (it == idx.symbols.end()) {
                idx.symbols[entry.key] = std::move(sym);
            } else {
                it->second.overloads.push_back(std::move(sym));
            }
        }
#else
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
#endif
    }
    return idx;
}

// Inspects a native C/C++ header via `clang++ -std=c++23 -Xclang -ast-dump=json -fsyntax-only`
// (or loads from `.cfire/cache/ast/<hash>.idx` if already cached).
[[nodiscard]] inline auto inspect_header(
    std::string_view header,
    const std::vector<std::string>& cflags,
    const std::filesystem::path& cache_dir = {}
) -> std::expected<HeaderIndex, std::string> {
    std::string clean_hdr(header);
    while (!clean_hdr.empty() && (clean_hdr.front() == '<' || clean_hdr.front() == '"')) clean_hdr.erase(clean_hdr.begin());
    while (!clean_hdr.empty() && (clean_hdr.back() == '>' || clean_hdr.back() == '"')) clean_hdr.pop_back();

    std::string key_str = "v3:" + clean_hdr;
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

    auto tmp_dir = std::filesystem::temp_directory_path() / std::format("cfire_clang_ast_{}", hash_key(key_str));
    std::filesystem::create_directories(tmp_dir);
    auto tu_path = tmp_dir / "tu.cpp";

    {
        std::ofstream out(tu_path);
        out << std::format("#include <{}>\n", clean_hdr);
    }

    std::string cmd = "clang++ -std=c++2b -Xclang -ast-dump=json -fsyntax-only";
    for (const auto& f : cflags) {
        cmd += " " + f;
    }
    cmd += " " + tu_path.string();

    int code = 0;
    std::string json_dump = run_command_capture(cmd, code);
    std::filesystem::remove_all(tmp_dir);

    if (code != 0) {
        return std::unexpected(std::format("clang AST ingestion failed for {}: {}", header, json_dump));
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
