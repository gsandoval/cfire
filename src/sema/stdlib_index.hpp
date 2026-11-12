#pragma once
// src/sema/stdlib_index.hpp — Standard Library Signature Catalog Index

#include "types.hpp"
#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cfire::frontend {

struct StdParamInfo {
    std::string name{};
    std::string intent{"in"};
    std::string type_str{};
    bool has_default = false;
    bool is_variadic = false;
    std::string default_text{};  // the `.cf` default expression (`true`, `""`, `{}`, `443`)
};

struct StdFnSigInfo {
    std::string name{};
    std::string module_import{};
    std::string ns_alias{};
    bool has_receiver = false;
    std::string receiver_type{};
    std::string receiver_intent{"in"};
    std::vector<StdParamInfo> params{};
    std::size_t min_args = 0;
    std::size_t max_args = 0;
    std::string return_type{};
    bool is_fallible = false;
    bool is_optional = false;
    std::string unwrapped_return_type{};
};

// Non-throwing map lookup for Sema: a missing key yields "" instead of std::out_of_range (CF-140).
template <typename Map>
[[nodiscard]] inline auto lookup_or(const Map& m, const std::string& key, std::string_view fallback) -> std::string {
    if (auto it = m.find(key); it != m.end()) return it->second;
    return std::string(fallback);
}

template <typename Map>
[[nodiscard]] inline auto lookup_or_empty(const Map& m, const std::string& key) -> std::string {
    return lookup_or(m, key, "");
}

struct StdlibCatalogIndex {
    std::unordered_map<std::string, std::string> ns_to_import{}; // "os" -> "core:os"
    std::unordered_map<std::string, std::unordered_map<std::string, StdFnSigInfo>> ns_fns{}; // "os" -> ("read_stdin_lines" -> sig)
    std::unordered_map<std::string, std::unordered_set<std::string>> ns_types_and_consts{}; // "os" -> {"ExecResult", "STDIN", ...}
    std::unordered_map<std::string, StdFnSigInfo> receiver_methods{}; // "fs.File.read_str" or "File.read_str" -> sig
    std::unordered_map<std::string, StdFnSigInfo> unambiguous_methods{}; // "read_str" -> sig

    [[nodiscard]] static auto parse_cf_sig(
        std::string_view mod_import,
        std::string_view ns,
        std::string_view fn_name,
        std::string_view cf_sig
    ) -> std::optional<StdFnSigInfo> {
        std::string_view s = cf_sig;
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
        if (!s.starts_with("fn ")) return std::nullopt;
        s.remove_prefix(3);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);

        StdFnSigInfo info;
        info.name = std::string(fn_name);
        info.module_import = std::string(mod_import);
        info.ns_alias = std::string(ns);

        if (!s.empty() && s.front() == '(') {
            auto rp = s.find(')');
            if (rp != std::string_view::npos) {
                std::string_view recv_sv = s.substr(1, rp - 1);
                info.has_receiver = true;
                if (recv_sv.starts_with("mut ")) {
                    info.receiver_intent = "mut";
                    recv_sv.remove_prefix(4);
                }
                if (auto col = recv_sv.find(':'); col != std::string_view::npos) {
                    std::string rtype = trim_str(recv_sv.substr(col + 1));
                    if (auto br = rtype.find('['); br != std::string::npos) rtype.erase(br);
                    info.receiver_type = std::move(rtype);
                }
                s.remove_prefix(rp + 1);
            }
        }
        if (auto dot = fn_name.find('.'); dot != std::string_view::npos) {
            std::string_view prefix = fn_name.substr(0, dot);
            // `Type.make` is a constructor (Q25: `make(ns.Type, args...)`), not a method.
            if (prefix != "Response" && prefix != "Duration" && fn_name.substr(dot + 1) != "make") {
                info.has_receiver = true;
                if (info.receiver_type.empty()) info.receiver_type = std::string(prefix);
            }
        }

        // Find parameter list '(' ... ')'
        int bracket_depth = 0;
        std::size_t lp = std::string_view::npos;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '[') ++bracket_depth;
            else if (s[i] == ']') --bracket_depth;
            else if (s[i] == '(' && bracket_depth == 0) {
                lp = i;
                break;
            }
        }
        if (lp == std::string_view::npos) return std::nullopt;

        int paren_depth = 0;
        std::size_t rp = std::string_view::npos;
        for (std::size_t i = lp; i < s.size(); ++i) {
            if (s[i] == '(') ++paren_depth;
            else if (s[i] == ')') {
                --paren_depth;
                if (paren_depth == 0) {
                    rp = i;
                    break;
                }
            }
        }
        if (rp == std::string_view::npos) return std::nullopt;

        std::string_view params_sv = s.substr(lp + 1, rp - lp - 1);
        std::string_view tail_sv = s.substr(rp + 1);
        if (auto arrow = tail_sv.find("->"); arrow != std::string_view::npos) {
            info.return_type = trim_str(tail_sv.substr(arrow + 2));
            if (info.return_type.ends_with('!')) {
                info.is_fallible = true;
                info.unwrapped_return_type = info.return_type.substr(0, info.return_type.size() - 1);
            } else if (info.return_type.ends_with('?')) {
                info.is_optional = true;
                info.unwrapped_return_type = info.return_type.substr(0, info.return_type.size() - 1);
            } else {
                info.unwrapped_return_type = info.return_type;
            }
        }

        // Split parameters at top-level commas
        std::size_t p_start = 0;
        int p_depth = 0;
        bool in_q = false;
        for (std::size_t i = 0; i <= params_sv.size(); ++i) {
            if (i < params_sv.size()) {
                char c = params_sv[i];
                if (c == '"' && (i == 0 || params_sv[i - 1] != '\\')) in_q = !in_q;
                else if (!in_q) {
                    if (c == '(' || c == '[' || c == '{') ++p_depth;
                    else if (c == ')' || c == ']' || c == '}') --p_depth;
                }
            }
            if ((i == params_sv.size() || (params_sv[i] == ',' && p_depth == 0 && !in_q))) {
                std::string p_tok = trim_str(params_sv.substr(p_start, i - p_start));
                p_start = i + 1;
                if (p_tok.empty()) continue;

                StdParamInfo pinfo;
                if (p_tok.contains("...")) {
                    pinfo.is_variadic = true;
                }
                // Check for top-level '=' default
                int d_depth = 0;
                bool d_q = false;
                std::size_t eq_pos = std::string::npos;
                for (std::size_t k = 0; k < p_tok.size(); ++k) {
                    char c = p_tok[k];
                    if (c == '"' && (k == 0 || p_tok[k - 1] != '\\')) d_q = !d_q;
                    else if (!d_q) {
                        if (c == '(' || c == '[' || c == '{') ++d_depth;
                        else if (c == ')' || c == ']' || c == '}') --d_depth;
                        else if (c == '=' && d_depth == 0) {
                            eq_pos = k;
                            break;
                        }
                    }
                }
                if (eq_pos != std::string::npos) {
                    pinfo.has_default = true;
                    pinfo.default_text = trim_str(std::string_view(p_tok).substr(eq_pos + 1));
                    p_tok = trim_str(std::string_view(p_tok).substr(0, eq_pos));
                }
                if (auto col = p_tok.find(':'); col != std::string::npos) {
                    std::string lhs = trim_str(std::string_view(p_tok).substr(0, col));
                    pinfo.type_str = trim_str(std::string_view(p_tok).substr(col + 1));
                    if (lhs.starts_with("mut ")) {
                        pinfo.intent = "mut";
                        pinfo.name = trim_str(std::string_view(lhs).substr(4));
                    } else if (lhs.starts_with("move ")) {
                        pinfo.intent = "move";
                        pinfo.name = trim_str(std::string_view(lhs).substr(5));
                    } else if (lhs.starts_with("out ")) {
                        pinfo.intent = "out";
                        pinfo.name = trim_str(std::string_view(lhs).substr(4));
                    } else {
                        if (lhs.starts_with("...")) lhs.erase(0, 3);
                        pinfo.name = std::move(lhs);
                    }
                } else {
                    pinfo.name = std::move(p_tok);
                }
                info.params.push_back(std::move(pinfo));
            }
        }

        bool any_variadic = false;
        std::size_t min_c = 0;
        for (const auto& p : info.params) {
            if (p.is_variadic) any_variadic = true;
            else if (!p.has_default) ++min_c;
        }
        info.min_args = min_c;
        info.max_args = any_variadic ? static_cast<std::size_t>(1024) : info.params.size();

        // Runtime parity overrides:
        if (ns == "cli" && fn_name == "parse") {
            info.is_fallible = false;
        }
        if (ns == "ai" && fn_name == "Client.ask") {
            info.min_args = 1;
            info.max_args = 3;
        }
        if (ns == "ai" && fn_name == "Client.embed") {
            info.min_args = 1;
            info.max_args = 2;
        }
        return info;
    }

    void register_fn(std::string_view mod_import, std::string_view ns, std::string_view fn_name, std::string_view cf_sig) {
        auto parsed = parse_cf_sig(mod_import, ns, fn_name, cf_sig);
        if (!parsed) {
            ns_types_and_consts[std::string(ns)].insert(std::string(fn_name));
            return;
        }
        if (parsed->has_receiver) {
            auto dot = fn_name.find('.');
            std::string short_m = (dot != std::string_view::npos) ? std::string(fn_name.substr(dot + 1)) : std::string(fn_name);
            receiver_methods[std::string(ns) + "." + std::string(fn_name)] = *parsed;
            receiver_methods[std::string(fn_name)] = *parsed;
            if (parsed->receiver_type == "Db") {
                receiver_methods["sqlite.Database." + short_m] = *parsed;
                receiver_methods["Database." + short_m] = *parsed;
            }
            unambiguous_methods.try_emplace(short_m, *parsed);
            if (!parsed->receiver_type.empty()) {
                ns_types_and_consts[std::string(ns)].insert(parsed->receiver_type);
            }
        } else {
            ns_fns[std::string(ns)][std::string(fn_name)] = std::move(*parsed);
        }
    }

    StdlibCatalogIndex() {
        for (const auto& mod : cfire::doc::stdlib_catalog()) {
            ns_to_import[mod.ns_alias] = mod.import_path;
            for (const auto& a : mod.aliases) {
                auto colon = a.rfind(':');
                auto slash = a.rfind('/');
                std::size_t sep = std::string::npos;
                if (colon != std::string::npos && slash != std::string::npos) sep = std::max(colon, slash);
                else if (colon != std::string::npos) sep = colon;
                else sep = slash;
                std::string alias_ns = (sep == std::string::npos) ? a : a.substr(sep + 1);
                if (!ns_to_import.contains(alias_ns)) {
                    ns_to_import[alias_ns] = a;
                }
            }
            for (const auto& t : mod.types) {
                ns_types_and_consts[mod.ns_alias].insert(t);
            }
            for (const auto& fn : mod.functions) {
                register_fn(mod.import_path, mod.ns_alias, fn.name, fn.cf_sig);
            }
        }
    }

    [[nodiscard]] static auto instance() -> const StdlibCatalogIndex& {
        static const StdlibCatalogIndex idx;
        return idx;
    }

    [[nodiscard]] auto has_namespace(std::string_view ns) const -> bool {
        return ns_to_import.contains(std::string(ns));
    }

    // Non-throwing lookup; returns "" for namespaces that are not stdlib modules (e.g. local packages).
    [[nodiscard]] auto import_path_for(std::string_view ns) const -> std::string {
        if (auto it = ns_to_import.find(std::string(ns)); it != ns_to_import.end()) return it->second;
        return "";
    }

    [[nodiscard]] auto find_ns_fn(std::string_view ns, std::string_view fn) const -> const StdFnSigInfo* {
        if (auto it = ns_fns.find(std::string(ns)); it != ns_fns.end()) {
            if (auto fit = it->second.find(std::string(fn)); fit != it->second.end()) {
                return &fit->second;
            }
        }
        return nullptr;
    }

    [[nodiscard]] auto is_known_ns_member(std::string_view ns, std::string_view member) const -> bool {
        if (find_ns_fn(ns, member) != nullptr) return true;
        if (auto it = ns_types_and_consts.find(std::string(ns)); it != ns_types_and_consts.end()) {
            if (it->second.contains(std::string(member))) return true;
        }
        return false;
    }

    [[nodiscard]] static auto edit_distance(std::string_view a, std::string_view b) -> std::size_t {
        std::vector<std::size_t> dp(b.size() + 1);
        for (std::size_t j = 0; j <= b.size(); ++j) dp[j] = j;
        for (std::size_t i = 1; i <= a.size(); ++i) {
            std::size_t prev = dp[0];
            dp[0] = i;
            for (std::size_t j = 1; j <= b.size(); ++j) {
                std::size_t temp = dp[j];
                std::size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
                dp[j] = std::min({dp[j] + 1, dp[j - 1] + 1, prev + cost});
                prev = temp;
            }
        }
        return dp[b.size()];
    }

    [[nodiscard]] auto suggest_closest_fn(std::string_view ns, std::string_view typo) const -> std::string {
        auto it = ns_fns.find(std::string(ns));
        if (it == ns_fns.end()) return "";
        std::string best;
        std::size_t best_dist = 4;
        for (const auto& [fname, _] : it->second) {
            std::size_t d = edit_distance(typo, fname);
            if (d < best_dist) {
                best_dist = d;
                best = fname;
            }
        }
        return best;
    }
};


} // namespace cfire::frontend
