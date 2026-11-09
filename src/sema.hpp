#pragma once
// src/sema.hpp — C++23 Semantic Analyzer, Intent Verifier, Exhaustive Match & Struct Tag Extractor

#include "ast.hpp"
#include "diagnostics.hpp"
#include "parser.hpp"
#include <expected>
#include <format>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cfire::frontend {

struct FieldValidationSpec {
    bool has_rules = false;
    bool has_min = false;
    std::string min_raw{};
    double min_num = 0.0;
    bool has_max = false;
    std::string max_raw{};
    double max_num = 0.0;
    bool has_min_len = false;
    std::size_t min_len = 0;
    bool has_max_len = false;
    std::size_t max_len = 0;
    bool non_empty = false;
    std::vector<std::string> oneof{};
};

struct StructTagInfo {
    std::string field_name{};
    TypeRef field_type{};
    std::string cli_long{};
    std::string cli_short{};
    std::string cli_help{};
    std::string json_key{};
    std::string db_column{};
    FieldValidationSpec validation{};
    std::unordered_map<std::string, std::string> custom_tags{};
};

struct CheckedFile {
    FileAST ast{};
    std::unordered_map<std::string, FnDecl> fn_sigs{};
    std::unordered_map<std::string, FnDecl> methods{};     // "Type::method" -> FnDecl
    std::unordered_map<std::string, std::string> method_owner{}; // "method" -> "Type" (primary owner for single-owner methods)
    std::unordered_map<std::string, std::unordered_set<std::string>> method_owners{}; // "method" -> set of "Type"s
    std::unordered_map<std::string, EnumDecl> enums{};     // "EnumName" -> EnumDecl
    std::unordered_map<std::string, std::string> variant_to_enum{}; // "VariantName" -> "EnumName"
    std::unordered_map<std::string, std::vector<StructTagInfo>> struct_tags{};
    std::unordered_map<std::string, int> last_use_move{}; // "fnName:varName" -> line
    std::unordered_map<std::string, std::vector<std::string>> cpp_fn_params{}; // C/C++ fn -> param QualTypes
    std::vector<std::string> warnings{};
    std::vector<std::string> errors{};
};

[[nodiscard]] inline auto trim_str(std::string_view sv) -> std::string {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) sv.remove_suffix(1);
    return std::string(sv);
}

struct StdParamInfo {
    std::string name{};
    std::string intent{"in"};
    std::string type_str{};
    bool has_default = false;
    bool is_variadic = false;
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
            if (prefix != "Response" && prefix != "Duration") {
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
            for (const auto& fn : mod.functions) {
                register_fn(mod.import_path, mod.ns_alias, fn.name, fn.cf_sig);
            }
        }
        // Additional runtime functions, types, and constants present in runtime headers:
        register_fn("core:os", "os", "exec", "fn exec(cmd: str) -> str!");
        for (const char* t : {"ExecResult", "TermSize", "RawTerminal"}) {
            ns_types_and_consts["os"].insert(t);
        }
        for (const char* t : {"OpenMode", "SeekWhence", "File"}) {
            ns_types_and_consts["fs"].insert(t);
        }
        register_fn("core:crypto/aes", "aes", "decrypt_gcm_str", "fn decrypt_gcm_str(key: []byte, nonce: []byte, ct: []byte, tag: []byte, aad: []byte = []) -> str!");
        ns_types_and_consts["aes"].insert("GcmResult");
        register_fn("core:crypto/hmac", "hmac", "sha256_bytes", "fn sha256_bytes(key: []byte, data: []byte) -> []byte");
        register_fn("core:crypto/sha256", "sha256", "hash_file_hex", "fn hash_file_hex(path: str) -> str!");
        register_fn("core:math", "math", "sqrt_f64", "fn sqrt_f64(x: f64) -> f64");
        for (const char* t : {"PI", "E"}) {
            ns_types_and_consts["math"].insert(t);
        }
        register_fn("core:str", "str", "replace_all", "fn replace_all(s: str, from: str, to: str) -> str");
        register_fn("core:str", "str", "bytes", "fn bytes(s: str) -> []byte");
        register_fn("core:time", "time", "duration_nanos", "fn duration_nanos(n: i64) -> Duration");
        register_fn("core:time", "time", "duration_micros", "fn duration_micros(us: i64) -> Duration");
        register_fn("core:time", "time", "duration_minutes", "fn duration_minutes(m: i64) -> Duration");
        register_fn("core:time", "time", "duration_hours", "fn duration_hours(h: i64) -> Duration");
        register_fn("core:time", "time", "from_unix_seconds", "fn from_unix_seconds(sec: i64) -> Time");
        for (const char* t : {"Duration", "Time"}) {
            ns_types_and_consts["time"].insert(t);
        }
        register_fn("core:sync", "sync", "new_mutex", "fn new_mutex() -> Mutex");
        if (auto it = ns_fns["sync"].find("make_channel"); it != ns_fns["sync"].end()) {
            it->second.max_args = 1;
        }
        if (auto it = ns_fns["cli"].find("parse"); it != ns_fns["cli"].end()) {
            it->second.min_args = 0;
            it->second.max_args = 1;
        }
        if (auto it = ns_fns["cli"].find("try_parse"); it != ns_fns["cli"].end()) {
            it->second.min_args = 0;
            it->second.max_args = 1;
        }
        for (const char* t : {"Channel", "Mutex"}) {
            ns_types_and_consts["sync"].insert(t);
        }
        register_fn("core:sort", "sort", "slice_by", "fn slice_by(mut s: []T, cmp: fn(T, T) -> bool)");
        register_fn("ext:ai", "ai", "create_zip", "fn create_zip(entries: map[str]str) -> []byte");
        for (const char* t : {"Provider", "Client", "Agent", "Document", "Message", "Tool", "Chunk"}) {
            ns_types_and_consts["ai"].insert(t);
        }
        register_fn("ext:webui", "webui", "clipboard_get", "fn clipboard_get() -> str");
        register_fn("ext:webui", "webui", "clipboard_set", "fn clipboard_set(text: str, concealed: bool = false, clear_after_ms: i64 = 0)");
        register_fn("ext:webui", "webui", "clipboard_set_secret", "fn clipboard_set_secret(text: str, clear_after_ms: i64 = 30000)");
        register_fn("ext:webui", "webui", "monospace_fonts", "fn monospace_fonts() -> []str");
        ns_types_and_consts["webui"].insert("App");
        for (const char* t : {"Waveform", "SfxPreset", "Wave", "Sound", "Stream"}) {
            ns_types_and_consts["audio"].insert(t);
        }
        for (const char* t : {"Level", "Format"}) {
            ns_types_and_consts["log"].insert(t);
        }
        for (const char* t : {"Db", "Database", "Stmt", "Statement", "Row"}) {
            ns_types_and_consts["sqlite"].insert(t);
        }
        for (const char* t : {"Request", "Response", "Router", "HttpServer", "Server", "SseEvent"}) {
            ns_types_and_consts["http"].insert(t);
        }
        for (const char* t : {"Url"}) {
            ns_types_and_consts["url"].insert(t);
        }
        for (const char* t : {"Conn", "Server", "Message"}) {
            ns_types_and_consts["websocket"].insert(t);
        }
        for (const char* t : {"Session", "Channel", "ExecResult"}) {
            ns_types_and_consts["ssh"].insert(t);
        }
        for (const char* t : {"CC", "CMake", "Make", "Git", "Archive", "Command", "Executable", "Library", "Step", "PkgConfig", "Link", "Include", "Tako"}) {
            ns_types_and_consts["build"].insert(t);
        }
    }

    [[nodiscard]] static auto instance() -> const StdlibCatalogIndex& {
        static const StdlibCatalogIndex idx;
        return idx;
    }

    [[nodiscard]] auto has_namespace(std::string_view ns) const -> bool {
        return ns_to_import.contains(std::string(ns));
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


class SemaFnChecker {
public:
    enum class NullState { NonNull, MaybeNull, Null };

    struct InvalidatedLoan {
        std::string reason{};
        int line = 1;
    };

    [[nodiscard]] static auto is_scalar_primitive_type_name(std::string_view t) -> bool {
        return t == "i8" || t == "i16" || t == "i32" || t == "i64" || t == "isize" || t == "int" ||
               t == "u8" || t == "u16" || t == "u32" || t == "u64" || t == "usize" || t == "uint" ||
               t == "byte" || t == "f32" || t == "f64" || t == "bool";
    }

    [[nodiscard]] static auto is_scalar_primitive_typeref(const TypeRef& t) -> bool {
        return !t.is_slice && !t.is_array && !t.is_map && !t.is_ptr && !t.is_mut_ptr &&
               t.smart_wrap.empty() && !t.is_tuple && !t.is_fn && !t.is_optional && !t.is_fallible &&
               is_scalar_primitive_type_name(t.name);
    }

    SemaFnChecker(std::string_view path, const FnDecl& fn, const CheckedFile& cf,
                  std::vector<std::string>& warnings, std::vector<std::string>& errors, bool resilient = false)
        : path_(path), pkg_name_(fn.package_name), fn_name_(fn.name), fn_ret_str_(format_type_ref_sema(fn.return_type)),
          cf_(cf), warnings_(warnings), errors_(errors), resilient_(resilient),
          fn_is_fallible_(fn.return_type.is_fallible || fn.name == "main" || fn.name.starts_with("__cfire_test_"))
    {
        for (const auto& imp : cf_.ast.imports) {
            if (imp.is_cpp) {
                has_cpp_imports_ = true;
                break;
            }
        }
        if (fn.receiver) {
            var_scope_depth_[fn.receiver->name] = 0;
            if (!fn.receiver->type.name.empty()) {
                var_types_[fn.receiver->name] = fn.receiver->type.name;
            }
            if (fn.receiver->intent == "mut") {
                mut_vars_.insert(fn.receiver->name);
            } else {
                imm_vars_.insert(fn.receiver->name);
            }
            if (fn.receiver->type.is_map) {
                map_vars_.insert(fn.receiver->name);
            }
            if (fn.receiver->type.is_array && !fn.receiver->type.array_len.empty()) {
                if (auto n = cfire::detail::parse_int<std::size_t>(fn.receiver->type.array_len)) {
                    fixed_array_lens_[fn.receiver->name] = *n;
                }
            }
        }
        for (const auto& p : fn.params) {
            var_scope_depth_[p.name] = 0;
            if (!p.type.smart_wrap.empty() && p.type.elem_type && !p.type.elem_type->name.empty()) {
                var_types_[p.name] = p.type.elem_type->name;
            } else if (!p.type.name.empty()) {
                var_types_[p.name] = p.type.name;
            }
            if (is_scalar_primitive_typeref(p.type)) {
                scalar_primitive_vars_.insert(p.name);
            }
            if (p.intent == "move") {
                move_params_.insert(p.name);
            }
            if (p.intent == "out") {
                warnings_.push_back(std::format(
                    "{}:{}:{}: warning: 'out' parameter is deprecated; use multi-return tuple '(T1, T2)' instead (cfire/DEPRECATED-OUT-PARAM)",
                    path_, p.line, p.col));
            }
            if (p.intent == "mut" || p.intent == "out") {
                mut_vars_.insert(p.name);
            } else {
                imm_vars_.insert(p.name);
            }
            if (p.type.is_map) {
                map_vars_.insert(p.name);
            }
            if (p.type.is_array && !p.type.array_len.empty()) {
                if (auto n = cfire::detail::parse_int<std::size_t>(p.type.array_len)) {
                    fixed_array_lens_[p.name] = *n;
                }
            }
            if (p.type.is_optional) {
                optional_vars_.insert(p.name);
            }
            if (p.type.is_fallible) {
                fallible_vars_.insert(p.name);
            }
            if (p.type.is_fn && p.type.fn_ret) {
                if (p.type.fn_ret->is_fallible) fn_ret_fallible_vars_.insert(p.name);
                if (p.type.fn_ret->is_optional) fn_ret_optional_vars_.insert(p.name);
            }
            if (p.type.is_ptr || p.type.is_mut_ptr || !p.type.smart_wrap.empty()) {
                raw_or_smart_ptr_vars_.insert(p.name);
            }
            if (p.type.is_ptr || p.type.is_mut_ptr || !p.type.smart_wrap.empty() || p.type.is_optional) {
                ptr_or_smart_vars_.insert(p.name);
                ptr_null_state_[p.name] = p.type.is_optional ? NullState::MaybeNull : NullState::NonNull;
            }
            if (!p.type.smart_wrap.empty()) {
                smart_ptr_vars_.insert(p.name);
            }
            if ((p.type.is_slice || p.type.is_array) && p.type.name == "byte") {
                byte_slice_vars_.insert(p.name);
            }
        }
    }

    SemaFnChecker(std::string_view path, const FnDecl& fn, const CheckedFile& cf, std::vector<std::string>& warnings)
        : SemaFnChecker(path, fn, cf, warnings, fallback_errors_, false) {}

    std::unordered_map<std::string, int> last_read_line{};

    [[nodiscard]] auto emit_error(std::string msg) -> std::expected<void, std::string> {
        if (resilient_) {
            errors_.push_back(std::move(msg));
            return {};
        }
        return std::unexpected(std::move(msg));
    }

    void invalidate_loans_for_owner(const std::string& owner, std::string_view reason, int line) {
        for (const auto& [borrower, orig] : borrow_origin_) {
            if (orig == owner) {
                invalidated_loans_[borrower] = InvalidatedLoan{std::string(reason), line};
            }
        }
    }

    [[nodiscard]] auto check_not_iterated_collection(const std::string& root, int line, int col)
        -> std::expected<void, std::string>
    {
        if (active_loop_collections_.contains(root)) {
            return emit_error(std::format(
                "{}:{}:{}: cannot structurally mutate collection '{}' while iterating over it (cfire/ITERATOR-INVALIDATION)",
                path_, line, col, root));
        }
        return {};
    }

    [[nodiscard]] auto record_var_read(const std::string& name, int line, int col)
        -> std::expected<void, std::string>
    {
        if (auto it = moved_at_line_.find(name); it != moved_at_line_.end()) {
            return emit_error(std::format(
                "{}:{}:{}: use of moved variable '{}' (previously moved at line {}) (cfire/USE-AFTER-MOVE)",
                path_, line, col, name, it->second));
        }
        if (auto it = invalidated_loans_.find(name); it != invalidated_loans_.end()) {
            return emit_error(std::format(
                "{}:{}:{}: use of dangling pointer/view '{}' ({}) (cfire/USE-AFTER-FREE)",
                path_, line, col, name, it->second.reason));
        }
        if (move_params_.contains(name)) {
            last_read_line[name] = line;
        }
        return {};
    }

    struct PathInfo {
        std::string root{};
        std::string path{};
    };

    [[nodiscard]] static auto extract_path_info(const ExprPtr& e) -> std::optional<PathInfo> {
        if (!e) return std::nullopt;
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            return PathInfo{id->name, id->name};
        }
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            if (mem->is_namespace) return std::nullopt;
            auto inner = extract_path_info(mem->object);
            if (!inner) return std::nullopt;
            return PathInfo{inner->root, inner->path + "." + mem->field};
        }
        if (const auto* idx = std::get_if<IndexExpr>(&e->node)) {
            auto inner = extract_path_info(idx->object);
            if (!inner) return std::nullopt;
            return PathInfo{inner->root, inner->path + "[*]"};
        }
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            if (un->op == "&" || un->op == "&mut" || un->op == "*") {
                return extract_path_info(un->operand);
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] auto resolve_root_owner(const std::string& var) const -> std::string {
        if (auto it = borrow_origin_.find(var); it != borrow_origin_.end()) {
            return it->second;
        }
        return var;
    }

    [[nodiscard]] auto extract_borrow_source(const ExprPtr& e, bool* is_direct_expr = nullptr) const -> std::optional<std::string> {
        if (is_direct_expr) *is_direct_expr = false;
        if (!e) return std::nullopt;
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            if (un->op == "&" || un->op == "&mut") {
                if (auto pi = extract_path_info(un->operand)) {
                    if (is_direct_expr) *is_direct_expr = true;
                    return resolve_root_owner(pi->root);
                }
            }
            return std::nullopt;
        }
        if (const auto* sl = std::get_if<SliceExpr>(&e->node)) {
            if (auto pi = extract_path_info(sl->object)) {
                if (is_direct_expr) *is_direct_expr = true;
                return resolve_root_owner(pi->root);
            }
            return std::nullopt;
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if ((mem->field == "data" || mem->field == "data_ptr" || mem->field == "byte_ptr") && call->args.empty()) {
                    if (auto pi = extract_path_info(mem->object)) {
                        if (is_direct_expr) *is_direct_expr = true;
                        return resolve_root_owner(pi->root);
                    }
                }
                if (mem->field == "byte_ptr_at" && call->args.size() == 1) {
                    if (auto pi = extract_path_info(mem->object)) {
                        if (is_direct_expr) *is_direct_expr = true;
                        return resolve_root_owner(pi->root);
                    }
                }
                if (const auto* mod_id = std::get_if<IdentExpr>(&mem->object->node);
                    mod_id && mod_id->name == "str" && !call->args.empty() &&
                    (mem->field == "byte_ptr" || mem->field == "byte_ptr_at")) {
                    if (auto pi = extract_path_info(call->args[0].value)) {
                        if (is_direct_expr) *is_direct_expr = true;
                        return resolve_root_owner(pi->root);
                    }
                }
            }
            return std::nullopt;
        }
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            if (auto it = borrow_origin_.find(id->name); it != borrow_origin_.end()) {
                return it->second;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] auto is_ptr_or_smart_init(const ExprPtr& e) const -> bool {
        if (!e) return false;
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            return un->op == "&" || un->op == "&mut";
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* id = std::get_if<IdentExpr>(&call->callee->node)) {
                if (id->name == "make" && call->type_args.size() == 1 && !call->type_args[0].smart_wrap.empty()) {
                    return true;
                }
            }
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if (mem->field == "data" || mem->field == "data_ptr" ||
                    mem->field == "byte_ptr" || mem->field == "byte_ptr_at") {
                    return true;
                }
            }
        }
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            return ptr_or_smart_vars_.contains(id->name);
        }
        return false;
    }

    [[nodiscard]] static auto is_const_zero_expr(const ExprPtr& e) -> bool {
        if (!e) return false;
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            if (un->op == "-" || un->op == "+") {
                return is_const_zero_expr(un->operand);
            }
            return false;
        }
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            if (lit->kind == TokenKind::Int) {
                if (auto v = cfire::detail::parse_int<std::int64_t>(lit->value)) {
                    return *v == 0;
                }
            } else if (lit->kind == TokenKind::Float) {
                bool has_zero = false;
                for (char c : lit->value) {
                    if (c == '0') has_zero = true;
                    else if (c != '.' && c != '_' && c != 'f' && c != 'F') return false;
                }
                return has_zero;
            }
        }
        return false;
    }

    [[nodiscard]] static auto is_dynamic_string_expr(const ExprPtr& e) -> bool {
        if (!e) return false;
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            if (lit->kind == TokenKind::String && lit->value.contains('{') && lit->value.contains('}')) {
                return true;
            }
            return false;
        }
        if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
            if (bin->op == "+") {
                bool left_static = bin->left && std::holds_alternative<LiteralExpr>(bin->left->node) && !is_dynamic_string_expr(bin->left);
                bool right_static = bin->right && std::holds_alternative<LiteralExpr>(bin->right->node) && !is_dynamic_string_expr(bin->right);
                return !(left_static && right_static);
            }
        }
        return false;
    }

    [[nodiscard]] auto is_namespace_ident_sema(std::string_view name) const -> bool {
        if (name == "fs" || name == "zstd" || name == "json" ||
            name == "sqlite" || name == "blake3" || name == "net" || name == "cli" ||
            name == "time" || name == "os" || name == "sync" || name == "str" ||
            name == "math" || name == "rand" || name == "log" || name == "regex" ||
            name == "collections" || name == "sort" || name == "unicode" ||
            name == "base64" || name == "hex" || name == "csv" ||
            name == "aes" || name == "hmac" || name == "sha256" ||
            name == "sodium" || name == "keychain" || name == "ssh" || name == "webui" ||
            name == "context" || name == "websocket" || name == "http" || name == "url" || name == "tls" ||
            name == "audio" || name == "ai" || name == "cpp" || name == "testing") {
            return true;
        }
        for (const auto& imp : cf_.ast.imports) {
            if (!imp.alias.empty() && imp.alias == name) return true;
            auto slash = imp.path.rfind('/');
            std::string_view tail = (slash == std::string::npos) ? std::string_view(imp.path) : std::string_view(imp.path).substr(slash + 1);
            if (tail == name) return true;
        }
        for (const auto& fn : cf_.ast.functions) {
            if (!fn.package_name.empty() && fn.package_name == name) return true;
        }
        for (const auto& st : cf_.ast.structs) {
            if (!st.package_name.empty() && st.package_name == name) return true;
        }
        for (const auto& en : cf_.ast.enums) {
            if (!en.package_name.empty() && en.package_name == name) return true;
        }
        for (const auto& gv : cf_.ast.global_vars) {
            if (!gv.package_name.empty() && gv.package_name == name) return true;
        }
        return false;
    }

    [[nodiscard]] static auto is_common_stdlib_method_sema(std::string_view name) -> bool {
        return name == "open" || name == "close" || name == "fd" || name == "is_open" || name == "is_tty" ||
               name == "poll" || name == "read" || name == "read_into" || name == "read_bytes" ||
               name == "read_exact" || name == "read_text" || name == "read_bytes_until" ||
               name == "read_text_until" || name == "read_str" || name == "read_line" ||
               name == "read_lines" || name == "read_all" || name == "write" ||
               name == "write_text" || name == "write_str" || name == "write_bytes" ||
               name == "flush" || name == "seek" ||
               name == "tell" || name == "size" || name == "len" || name == "empty" ||
               name == "clear" || name == "push" || name == "pop" || name == "get" || name == "set" ||
               name == "contains" || name == "keys" || name == "values" || name == "erase" ||
               name == "remove" || name == "send" || name == "recv" || name == "try_recv" ||
               name == "recv_timeout" || name == "bind" || name == "step" || name == "exec" ||
               name == "query" || name == "prepare" || name == "begin" || name == "commit" ||
               name == "rollback" || name == "join" || name == "detach" || name == "lock" ||
               name == "unlock" || name == "reset" || name == "has_value" || name == "value_or" ||
               name == "value" || name == "parse_i64" || name == "parse_i32" || name == "parse_u64" ||
               name == "parse_f64" || name == "parse_bool";
    }

    [[nodiscard]] auto resolve_member_call_sig(const MemberExpr& mem) const -> const FnDecl* {
        const auto* obj_id = mem.object ? std::get_if<IdentExpr>(&mem.object->node) : nullptr;
        if (obj_id && is_namespace_ident_sema(obj_id->name) && !is_ident_in_scope_sema(obj_id->name)) {
            if (auto it = cf_.fn_sigs.find(obj_id->name + "." + mem.field); it != cf_.fn_sigs.end()) {
                return &it->second;
            }
            return nullptr;
        }
        if (obj_id) {
            if (auto vt = var_types_.find(obj_id->name); vt != var_types_.end()) {
                if (auto mit = cf_.methods.find(vt->second + "::" + mem.field); mit != cf_.methods.end()) {
                    return &mit->second;
                }
            }
        }
        if (auto owner_it = cf_.method_owner.find(mem.field); owner_it != cf_.method_owner.end()) {
            auto owners_it = cf_.method_owners.find(mem.field);
            if (owners_it != cf_.method_owners.end() && owners_it->second.size() == 1 &&
                !is_common_stdlib_method_sema(mem.field)) {
                if (auto mit = cf_.methods.find(owner_it->second + "::" + mem.field); mit != cf_.methods.end()) {
                    return &mit->second;
                }
            }
        }
        return nullptr;
    }

    [[nodiscard]] auto expr_returns_optional_sema(const ExprPtr& e) const -> bool {
        if (!e) return false;
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            return optional_vars_.contains(id->name);
        }
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            return lit->kind == TokenKind::KwNone;
        }
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
            return expr_returns_optional_sema(tr->inner);
        }
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            if (!mem->is_namespace && mem->object) {
                std::string owner_t = infer_expr_type_name_sema(mem->object);
                if (!owner_t.empty()) {
                    for (const auto& st : cf_.ast.structs) {
                        if (st.name == owner_t) {
                            for (const auto& fld : st.fields) {
                                if (fld.name == mem->field) {
                                    return fld.type.is_optional && !fld.type.is_fallible;
                                }
                            }
                        }
                    }
                }
            }
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                if (cid->name == "some" || cid->name == "readln" || cid->name == "input") {
                    return true;
                }
                if (fn_ret_optional_vars_.contains(cid->name)) {
                    return true;
                }
                if (!pkg_name_.empty()) {
                    if (auto pit = cf_.fn_sigs.find(pkg_name_ + "." + cid->name); pit != cf_.fn_sigs.end()) {
                        return pit->second.return_type.is_optional && !pit->second.return_type.is_fallible;
                    }
                }
                if (auto it = cf_.fn_sigs.find(cid->name); it != cf_.fn_sigs.end()) {
                    return it->second.return_type.is_optional && !it->second.return_type.is_fallible;
                }
            } else if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                const auto& idx = StdlibCatalogIndex::instance();
                if (const auto* recv_id = std::get_if<IdentExpr>(&mem->object->node)) {
                    if ((is_namespace_ident_sema(recv_id->name) || idx.has_namespace(recv_id->name)) &&
                        !is_ident_in_scope_sema(recv_id->name)) {
                        if (const auto* sfn = idx.find_ns_fn(recv_id->name, mem->field)) {
                            if (sfn->is_optional) return true;
                        }
                        if (recv_id->name == "os" && mem->field == "getenv_opt") {
                            return true;
                        }
                        if (recv_id->name == "str" && (
                            mem->field == "find" ||
                            mem->field == "parse_i64" || mem->field == "parse_i32" || mem->field == "parse_u64" ||
                            mem->field == "parse_f64" || mem->field == "parse_bool")) {
                            return true;
                        }
                        if (recv_id->name == "cli" &&
                            (mem->field == "try_parse" || mem->field == "prompt" || mem->field == "prompt_password")) {
                            return true;
                        }
                        if (const auto* sig = resolve_member_call_sig(*mem)) {
                            return sig->return_type.is_optional && !sig->return_type.is_fallible;
                        }
                        return false;
                    }
                }
                if (mem->field == "parse_i64" || mem->field == "parse_i32" || mem->field == "parse_u64" ||
                    mem->field == "parse_f64" || mem->field == "parse_bool" ||
                    mem->field == "try_recv" || mem->field == "recv_timeout" ||
                    mem->field == "getenv_opt" || mem->field == "read_line") {
                    return true;
                }
                if (mem->field == "get" && call->args.size() == 1 && !call->block_arg) {
                    return true;
                }
                if (mem->field == "recv" && call->args.empty()) {
                    return true;
                }
                if (const auto* sig = resolve_member_call_sig(*mem)) {
                    return sig->return_type.is_optional && !sig->return_type.is_fallible;
                }
            }
        }
        return false;
    }

    [[nodiscard]] auto infer_expr_type_name_sema(const ExprPtr& e) const -> std::string {
        if (!e) return "";
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            if (lit->kind == TokenKind::String) return "str";
            if (lit->kind == TokenKind::Int) return "i64";
            if (lit->kind == TokenKind::Float) return "f64";
            if (lit->kind == TokenKind::KwTrue || lit->kind == TokenKind::KwFalse) return "bool";
        }
        if (const auto* st = std::get_if<StructLitExpr>(&e->node)) {
            return st->type_name;
        }
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
            return infer_expr_type_name_sema(tr->inner);
        }
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            return infer_expr_type_name_sema(or_ex->inner);
        }
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            if (auto it = var_types_.find(id->name); it != var_types_.end()) {
                return it->second;
            }
        }
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            if (!mem->is_namespace && mem->object) {
                std::string owner_t = infer_expr_type_name_sema(mem->object);
                if (!owner_t.empty()) {
                    if (auto sit = cf_.struct_tags.find(owner_t); sit != cf_.struct_tags.end()) {
                        for (const auto& f : sit->second) {
                            if (f.field_name == mem->field) {
                                return f.field_type.name;
                            }
                        }
                    }
                }
            }
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                if (cid->name == "make" && call->type_args.size() == 1) {
                    if (!call->type_args[0].smart_wrap.empty() && call->type_args[0].elem_type) {
                        return call->type_args[0].elem_type->name;
                    }
                    if (!call->type_args[0].name.empty()) {
                        return call->type_args[0].name;
                    }
                }
                if (cid->name == "[]byte" || cid->name == "[]u8" || cid->name == "str" ||
                    cid->name == "i8" || cid->name == "i16" || cid->name == "i32" || cid->name == "i64" ||
                    cid->name == "u8" || cid->name == "byte" || cid->name == "u16" || cid->name == "u32" ||
                    cid->name == "u64" || cid->name == "usize" || cid->name == "isize" ||
                    cid->name == "f32" || cid->name == "f64" || cid->name == "bool") {
                    return cid->name;
                }
                if (!pkg_name_.empty()) {
                    if (auto pit = cf_.fn_sigs.find(pkg_name_ + "." + cid->name); pit != cf_.fn_sigs.end()) {
                        if (!pit->second.return_type.smart_wrap.empty() && pit->second.return_type.elem_type) {
                            return pit->second.return_type.elem_type->name;
                        }
                        return pit->second.return_type.name;
                    }
                }
                if (auto it = cf_.fn_sigs.find(cid->name); it != cf_.fn_sigs.end()) {
                    if (!it->second.return_type.smart_wrap.empty() && it->second.return_type.elem_type) {
                        return it->second.return_type.elem_type->name;
                    }
                    return it->second.return_type.name;
                }
            } else if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if (const auto* recv_id = std::get_if<IdentExpr>(&mem->object->node)) {
                    if (recv_id->name == "os" && (mem->field == "getenv" || mem->field == "getenv_opt" || mem->field == "cwd" || mem->field == "temp_dir")) {
                        return "str";
                    }
                    if (recv_id->name == "fs" && mem->field == "read_text") {
                        return "str";
                    }
                    if ((recv_id->name == "fs" && mem->field == "read_bytes") ||
                        (recv_id->name == "hex" && mem->field == "decode") ||
                        (recv_id->name == "base64" && mem->field == "decode") ||
                        (recv_id->name == "rand" && mem->field == "bytes") ||
                        (recv_id->name == "sodium" && (mem->field == "random_bytes" || mem->field == "argon2id" || mem->field == "blake2b" || mem->field == "derive_subkey" || mem->field == "xchacha20poly1305_encrypt" || mem->field == "xchacha20poly1305_decrypt"))) {
                        return "[]byte";
                    }
                    if (const auto* std_sig = StdlibCatalogIndex::instance().find_ns_fn(recv_id->name, mem->field)) {
                        if (std_sig->unwrapped_return_type == "[]byte") return "[]byte";
                        if (!std_sig->unwrapped_return_type.empty() && std_sig->unwrapped_return_type != "void" &&
                            !std_sig->unwrapped_return_type.starts_with('[') && !std_sig->unwrapped_return_type.starts_with('&') &&
                            !std_sig->unwrapped_return_type.starts_with("map[") && !std_sig->unwrapped_return_type.starts_with("own[") &&
                            !std_sig->unwrapped_return_type.starts_with("shared[") && !std_sig->unwrapped_return_type.starts_with("tuple[") &&
                            !std_sig->unwrapped_return_type.starts_with('(') &&
                            std_sig->unwrapped_return_type != "T") {
                            return std_sig->unwrapped_return_type;
                        }
                    }
                    if (!is_ident_in_scope_sema(recv_id->name)) {
                        if (auto fit = cf_.fn_sigs.find(mem->field); fit != cf_.fn_sigs.end()) {
                            return fit->second.return_type.name;
                        }
                    }
                }
                if (mem->field == "bytes" && call->args.empty()) {
                    return "[]byte";
                }
                if (mem->field == "ocr" || mem->field == "detect_language") {
                    return "str";
                }
                if ((mem->field == "parse" || mem->field == "generate_struct" ||
                     mem->field == "generate_struct_from_file" || mem->field == "run_struct" ||
                     mem->field == "run_struct_validated") && call->type_args.size() == 1 &&
                    !call->type_args[0].name.empty()) {
                    return call->type_args[0].name;
                }
                if (const auto* sig = resolve_member_call_sig(*mem)) {
                    if (sig->return_type.is_slice && sig->return_type.name == "byte") return "[]byte";
                    return sig->return_type.name;
                }
                if (mem->object) {
                    std::string recv_t = infer_expr_type_name_sema(mem->object);
                    if (!recv_t.empty()) {
                        const auto& idx = StdlibCatalogIndex::instance();
                        if (auto mit = idx.receiver_methods.find(recv_t + "." + mem->field); mit != idx.receiver_methods.end()) {
                            return mit->second.unwrapped_return_type;
                        }
                        if (auto dcol = recv_t.find("::"); dcol != std::string::npos) {
                            std::string norm_t = recv_t.substr(0, dcol) + "." + recv_t.substr(dcol + 2);
                            if (auto mit = idx.receiver_methods.find(norm_t + "." + mem->field); mit != idx.receiver_methods.end()) {
                                return mit->second.unwrapped_return_type;
                            }
                        }
                    }
                }
            }
        }
        return "";
    }

    [[nodiscard]] auto is_non_byte_int_or_u8_expr(const ExprPtr& e) const -> bool {
        if (!e) return false;
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            return lit->kind == TokenKind::Int;
        }
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            if (un->op == "-" || un->op == "+") {
                return is_non_byte_int_or_u8_expr(un->operand);
            }
        }
        std::string t = infer_expr_type_name_sema(e);
        return t == "u8" || t == "i8" || t == "i16" || t == "i32" || t == "i64" ||
               t == "u16" || t == "u32" || t == "u64" || t == "usize" || t == "isize" || t == "int";
    }

    [[nodiscard]] auto is_byte_slice_expr(const ExprPtr& e) const -> bool {
        if (!e) return false;
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            return byte_slice_vars_.contains(id->name);
        }
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            if (!mem->is_namespace && mem->object) {
                std::string owner_t = infer_expr_type_name_sema(mem->object);
                if (!owner_t.empty()) {
                    if (auto sit = cf_.struct_tags.find(owner_t); sit != cf_.struct_tags.end()) {
                        for (const auto& f : sit->second) {
                            if (f.field_name == mem->field) {
                                return (f.field_type.is_slice || f.field_type.is_array) && f.field_type.name == "byte";
                            }
                        }
                    }
                }
            }
        }
        return infer_expr_type_name_sema(e) == "[]byte";
    }

    [[nodiscard]] auto is_str_slice_expr(const ExprPtr& e) const -> bool {
        if (!e) return false;
        if (const auto* sl = std::get_if<SliceExpr>(&e->node)) {
            return infer_expr_type_name_sema(sl->object) == "str";
        }
        return false;
    }

    [[nodiscard]] auto infer_expr_null_state(const ExprPtr& e) const -> std::optional<NullState> {
        if (!e) return std::nullopt;
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            if (lit->kind == TokenKind::KwNone) {
                return NullState::Null;
            }
            return std::nullopt;
        }
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            if (un->op == "&" || un->op == "&mut") {
                return NullState::NonNull;
            }
            return std::nullopt;
        }
        if (expr_returns_optional_sema(e)) {
            if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
                if (auto it = ptr_null_state_.find(id->name); it != ptr_null_state_.end()) {
                    return it->second;
                }
            }
            return NullState::MaybeNull;
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* id = std::get_if<IdentExpr>(&call->callee->node)) {
                if (id->name == "make" && call->type_args.size() == 1 && !call->type_args[0].smart_wrap.empty()) {
                    return NullState::NonNull;
                }
            }
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                if (mem->field == "data" || mem->field == "data_ptr" ||
                    mem->field == "byte_ptr" || mem->field == "byte_ptr_at") {
                    return NullState::NonNull;
                }
            }
            return std::nullopt;
        }
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            if (auto it = ptr_null_state_.find(id->name); it != ptr_null_state_.end()) {
                return it->second;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] static auto extract_null_check(const ExprPtr& cond) -> std::optional<std::pair<std::string, bool>> {
        if (!cond) return std::nullopt;
        if (const auto* bin = std::get_if<BinaryExpr>(&cond->node)) {
            if (bin->op == "!=" || bin->op == "==") {
                bool is_ne = (bin->op == "!=");
                if (bin->left && bin->right) {
                    const auto* l_id = std::get_if<IdentExpr>(&bin->left->node);
                    const auto* r_lit = std::get_if<LiteralExpr>(&bin->right->node);
                    if (l_id && r_lit && r_lit->kind == TokenKind::KwNone) {
                        return std::make_pair(l_id->name, is_ne);
                    }
                    const auto* r_id = std::get_if<IdentExpr>(&bin->right->node);
                    const auto* l_lit = std::get_if<LiteralExpr>(&bin->left->node);
                    if (r_id && l_lit && l_lit->kind == TokenKind::KwNone) {
                        return std::make_pair(r_id->name, is_ne);
                    }
                }
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] static auto paths_conflict(std::string_view a, std::string_view b) -> bool {
        if (a == b) return true;
        if (a.starts_with(b)) {
            char next = a[b.size()];
            if (next == '.' || next == '[') return true;
        }
        if (b.starts_with(a)) {
            char next = b[a.size()];
            if (next == '.' || next == '[') return true;
        }
        return false;
    }

    [[nodiscard]] static auto expr_start_loc(const ExprPtr& e) -> std::pair<int, int> {
        if (!e) return {1, 1};
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (call->callee) return expr_start_loc(call->callee);
        }
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            if (mem->object) return expr_start_loc(mem->object);
        }
        if (const auto* idx = std::get_if<IndexExpr>(&e->node)) {
            if (idx->object) return expr_start_loc(idx->object);
        }
        if (const auto* sl = std::get_if<SliceExpr>(&e->node)) {
            if (sl->object) return expr_start_loc(sl->object);
        }
        if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
            if (bin->left) return expr_start_loc(bin->left);
        }
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            if (or_ex->inner) return expr_start_loc(or_ex->inner);
        }
        return {e->line, e->col};
    }

    [[nodiscard]] static auto format_type_ref_sema(const TypeRef& t) -> std::string {
        std::string s;
        if (t.is_borrow) s += "&";
        else if (t.is_mut_borrow) s += "&mut ";
        else if (t.is_ptr) s += "*";
        else if (t.is_mut_ptr) s += "*mut ";
        if (t.is_slice) s += "[]";
        else if (t.is_array) s += "[" + t.array_len + "]";
        if (t.is_tuple) {
            s += "(";
            for (std::size_t i = 0; i < t.tuple_types.size(); ++i) {
                if (i > 0) s += ", ";
                s += format_type_ref_sema(t.tuple_types[i]);
            }
            s += ")";
        } else if (!t.smart_wrap.empty() && t.elem_type) {
            s += t.smart_wrap + "[" + format_type_ref_sema(*t.elem_type) + "]";
        } else {
            s += t.name.empty() ? "void" : t.name;
        }
        if (t.is_optional) s += "?";
        if (t.is_fallible) s += "!";
        return s;
    }

    [[nodiscard]] auto is_stdlib_ns_imported(std::string_view ns) const -> bool {
        if (ns == "build" || ns == "testing" || ns == "cpp") return true;
        const auto& idx = StdlibCatalogIndex::instance();
        auto mod_it = idx.ns_to_import.find(std::string(ns));
        std::string_view expected_mod = (mod_it != idx.ns_to_import.end()) ? std::string_view(mod_it->second) : std::string_view{};
        for (const auto& imp : cf_.ast.imports) {
            if (!imp.alias.empty() && imp.alias == ns) return true;
            if (!expected_mod.empty() && imp.path == expected_mod) return true;
            auto colon = imp.path.rfind(':');
            auto slash = imp.path.rfind('/');
            std::size_t sep = std::string::npos;
            if (colon != std::string::npos && slash != std::string::npos) sep = std::max(colon, slash);
            else if (colon != std::string::npos) sep = colon;
            else sep = slash;
            std::string_view tail = (sep == std::string::npos) ? std::string_view(imp.path) : std::string_view(imp.path).substr(sep + 1);
            if (tail == ns) return true;
            if (ns == "rl" && tail == "raylib") return true;
            if (ns == "rg" && tail == "raygui") return true;
        }
        return false;
    }

    [[nodiscard]] auto check_stdlib_ns_imported(std::string_view ns, int line, int col) -> std::expected<void, std::string> {
        const auto& idx = StdlibCatalogIndex::instance();
        if (!idx.has_namespace(ns) || is_ident_in_scope_sema(std::string(ns))) return {};
        if (is_stdlib_ns_imported(ns)) return {};
        if (reported_missing_imports_.contains(std::string(ns))) return {};
        reported_missing_imports_.insert(std::string(ns));
        std::string mod_path = idx.ns_to_import.at(std::string(ns));
        return emit_error(std::format(
            "{}:{}:{}: module '{}' is not imported; add 'import \"{}\"' (cfire/MISSING-IMPORT)",
            path_, line, col, ns, mod_path));
    }

    [[nodiscard]] auto is_ident_in_scope_sema(const std::string& ident) const -> bool {
        if (local_defined_vars_.contains(ident) || mut_vars_.contains(ident) ||
            imm_vars_.contains(ident) || move_params_.contains(ident)) {
            return true;
        }
        if (cf_.fn_sigs.contains(ident)) return true;
        if (!pkg_name_.empty() && cf_.fn_sigs.contains(pkg_name_ + "." + ident)) return true;
        for (const auto& gv : cf_.ast.global_vars) {
            if (gv.name == ident) return true;
        }
        return false;
    }

    [[nodiscard]] static auto is_builtin_ident_sema(std::string_view name) -> bool {
        return name == "_" || name == "break" || name == "continue" ||
               name == "nullptr" || name == "null" ||
               name == "println" || name == "print" || name == "eprintln" || name == "eprint" ||
               name == "format" || name == "readln" || name == "input" || name == "black_box" ||
               name == "panic" || name == "assert" ||
               name == "assert_eq" || name == "assert_ne" || name == "assert_true" || name == "assert_false" ||
               name == "assert_err" || name == "assert_ok" || name == "error" || name == "make" ||
               name == "len" || name == "cap" || name == "append" || name == "copy" || name == "delete" ||
               name == "close" || name == "exit" || name == "some" || name == "none" || name == "ok" ||
               name == "err" || name == "own" || name == "shared" || name == "async" ||
               name == "i8" || name == "i16" || name == "i32" || name == "i64" || name == "isize" || name == "int" ||
               name == "u8" || name == "u16" || name == "u32" || name == "u64" || name == "usize" || name == "uint" ||
               name == "byte" || name == "f32" || name == "f64" || name == "bool" || name == "str" ||
               name == "char" || name == "rune" || name == "void" || name == "any" ||
               name == "c_int" || name == "c_char" || name == "c_void" || name == "c_long" ||
               name == "c_ulong" || name == "c_float" || name == "c_double" ||
               name.starts_with("[]") || name.starts_with("[") || name.starts_with("map[") ||
               name.starts_with("*") || name.starts_with("&");
    }

    [[nodiscard]] auto is_known_identifier_sema(const std::string& name) const -> bool {
        if (name.empty() || is_builtin_ident_sema(name)) return true;
        if (is_ident_in_scope_sema(name)) return true;
        if (is_namespace_ident_sema(name)) return true;
        if (cf_.struct_tags.contains(name) || cf_.enums.contains(name) || cf_.variant_to_enum.contains(name)) return true;
        if (cf_.cpp_fn_params.contains(name)) return true;
        for (const auto& st : cf_.ast.structs) {
            if (st.name == name) return true;
        }
        // Check compound names like `Shape.Circle`, `audio.Waveform::Sine`, `Waveform::Sine`, `http.Response`
        auto dot = name.find('.');
        auto dcol = name.find("::");
        std::size_t sep = std::min(dot, dcol);
        if (sep != std::string::npos) {
            std::string prefix = name.substr(0, sep);
            if (is_known_identifier_sema(prefix)) return true;
            if (prefix == "Waveform" || prefix == "SfxPreset" || prefix == "OpenMode" ||
                prefix == "SeekWhence" || prefix == "Level" || prefix == "Format" || prefix == "Provider") {
                return true;
            }
            if (dcol != std::string::npos) {
                std::string suffix = name.substr(dcol + 2);
                if (cf_.variant_to_enum.contains(suffix)) return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto expr_returns_fallible_sema(const ExprPtr& e, std::string* out_callee = nullptr, std::string* out_ret = nullptr) const -> bool {
        if (!e) return false;
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                const FnDecl* sig = nullptr;
                if (!pkg_name_.empty()) {
                    if (auto pit = cf_.fn_sigs.find(pkg_name_ + "." + cid->name); pit != cf_.fn_sigs.end()) {
                        sig = &pit->second;
                    }
                }
                if (!sig) {
                    if (auto it = cf_.fn_sigs.find(cid->name); it != cf_.fn_sigs.end()) {
                        sig = &it->second;
                    }
                }
                if (sig && sig->return_type.is_fallible) {
                    if (out_callee) *out_callee = cid->name;
                    if (out_ret) *out_ret = format_type_ref_sema(sig->return_type);
                    return true;
                }
                if (fn_ret_fallible_vars_.contains(cid->name)) {
                    if (out_callee) *out_callee = cid->name;
                    if (out_ret) *out_ret = "T!";
                    return true;
                }
            } else if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                const auto* obj_id = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
                const auto& idx = StdlibCatalogIndex::instance();
                if (obj_id && idx.has_namespace(obj_id->name) && !is_ident_in_scope_sema(obj_id->name)) {
                    if (const auto* sfn = idx.find_ns_fn(obj_id->name, mem->field)) {
                        if (sfn->is_fallible) {
                            if (out_callee) *out_callee = obj_id->name + "." + mem->field;
                            if (out_ret) *out_ret = sfn->return_type;
                            return true;
                        }
                    }
                }
                if (const auto* sig = resolve_member_call_sig(*mem)) {
                    if (sig->return_type.is_fallible) {
                        if (out_callee) *out_callee = mem->field;
                        if (out_ret) *out_ret = format_type_ref_sema(sig->return_type);
                        return true;
                    }
                }
                if (obj_id) {
                    if (auto vt = var_types_.find(obj_id->name); vt != var_types_.end()) {
                        if (auto mit = idx.receiver_methods.find(vt->second + "." + mem->field); mit != idx.receiver_methods.end()) {
                            if (mit->second.is_fallible) {
                                if (out_callee) *out_callee = obj_id->name + "." + mem->field;
                                if (out_ret) *out_ret = mit->second.return_type;
                                return true;
                            }
                        }
                    }
                }
                if (mem->object) {
                    std::string recv_t = infer_expr_type_name_sema(mem->object);
                    if (!recv_t.empty()) {
                        if (auto mit = idx.receiver_methods.find(recv_t + "." + mem->field); mit != idx.receiver_methods.end()) {
                            if (mit->second.is_fallible) {
                                if (out_callee) *out_callee = mem->field;
                                if (out_ret) *out_ret = mit->second.return_type;
                                return true;
                            }
                        }
                    }
                }
            }
        }
        return false;
    }

    [[nodiscard]] auto is_expr_fallible_for_unwrap_sema(const ExprPtr& e) const -> bool {
        if (!e) return false;
        if (std::holds_alternative<TryExpr>(e->node) || std::holds_alternative<OrExpr>(e->node)) {
            return false;
        }
        if (expr_returns_fallible_sema(e)) return true;
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            return fallible_vars_.contains(id->name);
        }
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            if (!mem->is_namespace && mem->object) {
                std::string owner_t = infer_expr_type_name_sema(mem->object);
                if (!owner_t.empty()) {
                    for (const auto& st : cf_.ast.structs) {
                        if (st.name == owner_t) {
                            for (const auto& fld : st.fields) {
                                if (fld.name == mem->field) return fld.type.is_fallible;
                            }
                        }
                    }
                }
            }
            return false;
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                if (cid->name == "ok" || cid->name == "err" || cid->name == "error" || cid->name == "Err") {
                    return true;
                }
                if (fn_ret_fallible_vars_.contains(cid->name)) {
                    return true;
                }
                if ((has_cpp_imports_ || seen_cpp_block_) && !is_known_identifier_sema(cid->name)) {
                    return true;
                }
                return false;
            }
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                const auto& idx = StdlibCatalogIndex::instance();
                const auto* obj_id = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
                if (obj_id && !is_ident_in_scope_sema(obj_id->name)) {
                    if (obj_id->name == "cpp" || obj_id->name == "rl" || obj_id->name == "rg") {
                        return true;
                    }
                    if ((obj_id->name == "fs" && (mem->field == "read_all" || mem->field == "write_all")) ||
                        (obj_id->name == "os" && mem->field == "socketpair")) {
                        return true;
                    }
                    if (idx.has_namespace(obj_id->name)) {
                        if (const auto* sfn = idx.find_ns_fn(obj_id->name, mem->field)) {
                            return sfn->is_fallible;
                        }
                        return false;
                    }
                }
                if (const auto* sig = resolve_member_call_sig(*mem)) {
                    return sig->return_type.is_fallible;
                }
                if (mem->field == "read_str" || mem->field == "write_str" ||
                    mem->field == "read_all" || mem->field == "read" || mem->field == "write" ||
                    mem->field == "call_rpc" || mem->field == "run_webview_test" ||
                    mem->field == "generate_struct" || mem->field == "generate_struct_from_file" ||
                    mem->field == "run_struct" || mem->field == "run_struct_validated" ||
                    mem->field == "embed" || mem->field == "embed_image" ||
                    mem->field == "ocr" || mem->field == "detect_language" ||
                    mem->field == "stream" || mem->field == "ask" || mem->field == "tx") {
                    return true;
                }
                if (auto uit = idx.unambiguous_methods.find(mem->field); uit != idx.unambiguous_methods.end()) {
                    if (uit->second.is_fallible) return true;
                }
                std::string dot_suffix = "." + mem->field;
                for (const auto& [k, minfo] : idx.receiver_methods) {
                    if (k.ends_with(dot_suffix) && minfo.is_fallible) return true;
                }
                std::string col_suffix = "::" + mem->field;
                for (const auto& [k, mdecl] : cf_.methods) {
                    if (k.ends_with(col_suffix) && mdecl.return_type.is_fallible) return true;
                }
                if (has_cpp_imports_ || seen_cpp_block_) return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto is_expr_optional_for_unwrap_sema(const ExprPtr& e) const -> bool {
        if (!e) return false;
        if (std::holds_alternative<TryExpr>(e->node) || std::holds_alternative<OrExpr>(e->node)) {
            return false;
        }
        if (expr_returns_optional_sema(e)) return true;
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            if (const auto* cid = std::get_if<IdentExpr>(&call->callee->node)) {
                if ((has_cpp_imports_ || seen_cpp_block_) && !is_known_identifier_sema(cid->name)) {
                    return true;
                }
                return false;
            }
            if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                const auto& idx = StdlibCatalogIndex::instance();
                const auto* obj_id = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
                if (obj_id && !is_ident_in_scope_sema(obj_id->name)) {
                    if (obj_id->name == "cpp" || obj_id->name == "rl" || obj_id->name == "rg") {
                        return true;
                    }
                    if (idx.has_namespace(obj_id->name)) {
                        if (const auto* sfn = idx.find_ns_fn(obj_id->name, mem->field)) {
                            return sfn->is_optional;
                        }
                        return false;
                    }
                }
                if (const auto* sig = resolve_member_call_sig(*mem)) {
                    return sig->return_type.is_optional && !sig->return_type.is_fallible;
                }
                if (auto uit = idx.unambiguous_methods.find(mem->field); uit != idx.unambiguous_methods.end()) {
                    if (uit->second.is_optional) return true;
                }
                std::string dot_suffix = "." + mem->field;
                for (const auto& [k, minfo] : idx.receiver_methods) {
                    if (k.ends_with(dot_suffix) && minfo.is_optional) return true;
                }
                std::string col_suffix = "::" + mem->field;
                for (const auto& [k, mdecl] : cf_.methods) {
                    if (k.ends_with(col_suffix) && mdecl.return_type.is_optional) return true;
                }
                if (has_cpp_imports_ || seen_cpp_block_) return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto validate_move_operand(
        const ExprPtr& operand,
        bool required_by_sig,
        int line,
        int col
    ) -> std::expected<void, std::string> {
        if (!operand) return {};
        if (std::holds_alternative<UnaryExpr>(operand->node)) {
            return emit_error(std::format(
                "{}:{}:{}: cannot use 'move' on temporary or unary expression; 'move' only applies to owned variables (cfire/INVALID-MOVE)",
                path_, line, col));
        }
        auto pi = extract_path_info(operand);
        if (!pi) {
            return emit_error(std::format(
                "{}:{}:{}: cannot use 'move' on temporary or literal expression; 'move' only applies to owned variables (cfire/INVALID-MOVE)",
                path_, line, col));
        }
        if (pi->path == pi->root && scalar_primitive_vars_.contains(pi->root) && !required_by_sig) {
            std::string tname = var_types_.contains(pi->root) ? var_types_.at(pi->root) : "primitive";
            return emit_error(std::format(
                "{}:{}:{}: 'move' has no effect on Copy primitive variable '{}' of type '{}' (cfire/INVALID-MOVE)",
                path_, line, col, pi->root, tname));
        }
        return {};
    }

    [[nodiscard]] static auto is_effectful_stmt_expr(const ExprPtr& e) -> bool {
        if (!e) return true;
        if (std::holds_alternative<CallExpr>(e->node) ||
            std::holds_alternative<MatchExpr>(e->node)) {
            return true;
        }
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
            return is_effectful_stmt_expr(tr->inner);
        }
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            return is_effectful_stmt_expr(or_ex->inner) || !or_ex->handler_body.empty();
        }
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            return id->name == "break" || id->name == "continue";
        }
        return false;
    }

    [[nodiscard]] static auto extract_root_ident_sema(const ExprPtr& e) -> std::string {
        if (!e) return "";
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) return id->name;
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) return extract_root_ident_sema(mem->object);
        if (const auto* idx = std::get_if<IndexExpr>(&e->node)) return extract_root_ident_sema(idx->object);
        if (const auto* sl = std::get_if<SliceExpr>(&e->node)) return extract_root_ident_sema(sl->object);
        if (const auto* call = std::get_if<CallExpr>(&e->node)) return extract_root_ident_sema(call->callee);
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) return extract_root_ident_sema(un->operand);
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) return extract_root_ident_sema(tr->inner);
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) return extract_root_ident_sema(or_ex->inner);
        if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
            auto l = extract_root_ident_sema(bin->left);
            return !l.empty() ? l : extract_root_ident_sema(bin->right);
        }
        return "";
    }

    [[nodiscard]] auto check_expr(const ExprPtr& e) -> std::expected<void, std::string> {
        if (!e) return {};
        if (const auto* id = std::get_if<IdentExpr>(&e->node)) {
            if (id->name == "break" || id->name == "continue") {
                if (loop_depth_ == 0) {
                    return emit_error(std::format(
                        "{}:{}:{}: '{}' cannot be used outside of a 'for' loop (cfire/BREAK-OUTSIDE-LOOP)",
                        path_, e->line, e->col, id->name));
                }
                return {};
            }
            if (auto r = record_var_read(id->name, e->line, e->col); !r) return r;
            auto dot = id->name.find('.');
            auto dcol = id->name.find("::");
            std::size_t sep = std::min(dot, dcol);
            if (sep != std::string::npos) {
                std::string prefix = id->name.substr(0, sep);
                if (auto r = check_stdlib_ns_imported(prefix, e->line, e->col); !r) return r;
            }
            if (!has_cpp_imports_ && !seen_cpp_block_ && !is_known_identifier_sema(id->name)) {
                return emit_error(std::format(
                    "{}:{}:{}: undeclared identifier '{}' (cfire/UNDECLARED-IDENT)",
                    path_, e->line, e->col, id->name));
            }
            return {};
        }
        if (const auto* lit = std::get_if<LiteralExpr>(&e->node)) {
            return check_literal_expr(*lit, e->line);
        }
        if (const auto* un = std::get_if<UnaryExpr>(&e->node)) {
            return check_unary_expr(*un, e->line, e->col);
        }
        if (const auto* bin = std::get_if<BinaryExpr>(&e->node)) {
            return check_binary_expr(*bin, e->line, e->col);
        }
        if (const auto* mem = std::get_if<MemberExpr>(&e->node)) {
            return check_member_expr(*mem, e->line, e->col);
        }
        if (const auto* idx = std::get_if<IndexExpr>(&e->node)) {
            return check_index_expr(*idx, e->line, e->col);
        }
        if (const auto* sl = std::get_if<SliceExpr>(&e->node)) {
            if (auto r = check_expr(sl->object); !r) return r;
            if (sl->start) if (auto r = check_expr(sl->start); !r) return r;
            if (sl->end) if (auto r = check_expr(sl->end); !r) return r;
            return {};
        }
        if (const auto* tr = std::get_if<TryExpr>(&e->node)) {
            if (tr->inner) unwrapped_exprs_.insert(tr->inner);
            return check_try_expr(*tr, e->line, e->col);
        }
        if (const auto* or_ex = std::get_if<OrExpr>(&e->node)) {
            if (or_ex->inner) unwrapped_exprs_.insert(or_ex->inner);
            return check_or_expr(*or_ex, e->line, e->col, e);
        }
        if (const auto* st = std::get_if<StructLitExpr>(&e->node)) {
            auto dot = st->type_name.find('.');
            auto dcol = st->type_name.find("::");
            std::size_t sep = std::min(dot, dcol);
            if (sep != std::string::npos) {
                std::string prefix = st->type_name.substr(0, sep);
                if (auto r = check_stdlib_ns_imported(prefix, e->line, e->col); !r) return r;
            }
            for (const auto& f : st->fields) {
                if (auto r = check_expr(f.value); !r) return r;
            }
            return {};
        }
        if (const auto* arr = std::get_if<ArrayLitExpr>(&e->node)) {
            for (const auto& el : arr->elements) {
                if (auto r = check_expr(el); !r) return r;
            }
            return {};
        }
        if (const auto* tup = std::get_if<TupleExpr>(&e->node)) {
            for (const auto& el : tup->elements) {
                if (auto r = check_expr(el); !r) return r;
            }
            return {};
        }
        if (const auto* cl = std::get_if<ClosureExpr>(&e->node)) {
            ++in_closure_depth_;
            int saved_loop_depth = loop_depth_;
            int saved_invalid_unwrap = in_invalid_unwrap_context_;
            loop_depth_ = 0;
            in_invalid_unwrap_context_ = 0;
            auto cl_guard = cfire::scope_exit([this, saved_loop_depth, saved_invalid_unwrap] {
                --in_closure_depth_;
                loop_depth_ = saved_loop_depth;
                in_invalid_unwrap_context_ = saved_invalid_unwrap;
            });
            ScopeGuard sg(*this);
            for (std::size_t i = 0; i < cl->params.size(); ++i) {
                bool is_mut = (i < cl->param_intents.size()) &&
                              (cl->param_intents[i] == "mut" || cl->param_intents[i] == "out");
                bind_scoped_var(cl->params[i], is_mut);
            }
            return check_block_stmts(cl->body);
        }
        if (const auto* call = std::get_if<CallExpr>(&e->node)) {
            return check_call_expr(*call, e->line, e->col, e);
        }
        if (const auto* me = std::get_if<MatchExpr>(&e->node)) {
            return check_match_common(me->target, me->is_move, me->arms, me->line, me->col, true);
        }
        return {};
    }

    [[nodiscard]] auto check_try_expr(const TryExpr& tr, int line, int col) -> std::expected<void, std::string> {
        if (in_defer_depth_ > 0) {
            if (auto r = emit_error(std::format(
                "{}:{}:{}: cannot use 'try' inside a 'defer' block; handle errors explicitly with 'or' (cfire/TRY-IN-DEFER)",
                path_, line, col)); !r) return r;
        } else if (!fn_is_fallible_ && in_closure_depth_ == 0) {
            std::string ret_desc = fn_ret_str_.empty() ? "void" : fn_ret_str_;
            if (auto r = emit_error(std::format(
                "{}:{}:{}: cannot use 'try' in non-fallible function '{}' (returns '{}'); mark return type with '!' or handle error with 'or' (cfire/TRY-IN-NON-FALLIBLE-FN)",
                path_, line, col, fn_name_.empty() ? "<anonymous>" : fn_name_, ret_desc)); !r) return r;
        }
        if (in_invalid_unwrap_context_ > 0) {
            if (auto r = emit_error(std::format(
                "{}:{}:{}: cannot use 'try' inside short-circuit operand or 'for' loop condition; hoist into a statement or use 'or fallback' (cfire/INVALID-TRY-CONTEXT)",
                path_, line, col)); !r) return r;
        }
        std::size_t errors_before = errors_.size();
        if (auto r = check_expr(tr.inner); !r) return r;
        if (errors_.size() == errors_before && !has_cpp_imports_ && !seen_cpp_block_) {
            if (!is_expr_fallible_for_unwrap_sema(tr.inner)) {
                if (is_expr_optional_for_unwrap_sema(tr.inner)) {
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: cannot use 'try' on optional 'T?' value; 'try' only propagates fallible 'T!' errors — use 'or fallback' or 'or {{ ... }}' instead (cfire/TRY-ON-OPTIONAL)",
                        path_, line, col)); !r) return r;
                } else {
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: 'try' used on non-fallible expression; remove 'try' (cfire/REDUNDANT-TRY)",
                        path_, line, col)); !r) return r;
                }
            }
        }
        return {};
    }

    [[nodiscard]] static auto is_likely_interpolation_placeholder(std::string_view sv, const InterpPlaceholder& ph) -> bool {
        if (ph.start_pos + 1 >= sv.size()) return false;
        char first_raw = sv[ph.start_pos + 1];
        if (std::isspace(static_cast<unsigned char>(first_raw))) return false;
        if (!(std::isalnum(static_cast<unsigned char>(first_raw)) || first_raw == '_' || first_raw == '(' || first_raw == '*')) {
            return false;
        }
        // Exclude JSON/CSS/code blocks containing ';' or single ':'
        bool in_q = false;
        for (std::size_t i = 0; i < ph.expr_text.size(); ++i) {
            char c = ph.expr_text[i];
            if (c == '"' && (i == 0 || ph.expr_text[i - 1] != '\\')) in_q = !in_q;
            else if (!in_q) {
                if (c == ';') return false;
                if (c == ':') {
                    bool is_dcol = (i + 1 < ph.expr_text.size() && ph.expr_text[i + 1] == ':') ||
                                   (i > 0 && ph.expr_text[i - 1] == ':');
                    if (!is_dcol) return false;
                }
            }
        }
        // Exclude pure integer regex quantifiers like `\d{4}`
        bool all_digits = !ph.expr_text.empty() && std::ranges::all_of(ph.expr_text, [](unsigned char c) {
            return std::isdigit(c) || c == ',';
        });
        if (all_digits) return false;
        return true;
    }

    [[nodiscard]] auto check_literal_expr(const LiteralExpr& lit, int line) -> std::expected<void, std::string> {
        if (lit.kind == TokenKind::String && lit.value.contains('{')) {
            auto placeholders = scan_interpolation_placeholders(lit.value);
            for (const auto& ph : placeholders) {
                if (!is_likely_interpolation_placeholder(lit.value, ph)) continue;
                auto parsed = parse_expr_snippet(path_, ph.expr_text, line);
                if (!parsed) {
                    if (auto r = emit_error(std::format(
                        "{}:{}:1: invalid expression '{}' in string interpolation (cfire/INVALID-INTERPOLATION)",
                        path_, line, ph.expr_text)); !r) return r;
                    continue;
                }
                if (auto r = check_expr(parsed->expr); !r) return r;
            }
        }
        return {};
    }

    [[nodiscard]] auto check_unary_expr(const UnaryExpr& un, int line, int col) -> std::expected<void, std::string> {
        if (auto r = check_expr(un.operand); !r) return r;
        if (un.op == "move") {
            if (auto r = validate_move_operand(un.operand, false, line, col); !r) return r;
            if (auto pi = extract_path_info(un.operand)) {
                if (auto r = check_not_iterated_collection(pi->root, line, col); !r) return r;
                moved_at_line_[pi->root] = line;
                invalidate_loans_for_owner(
                    pi->root,
                    std::format("underlying owner '{}' was moved at line {}", pi->root, line),
                    line);
            }
        } else if (un.op == "*" && un.operand) {
            if (const auto* id = std::get_if<IdentExpr>(&un.operand->node)) {
                if (auto it = ptr_null_state_.find(id->name); it != ptr_null_state_.end()) {
                    if (it->second == NullState::Null) {
                        if (auto r = emit_error(std::format(
                            "{}:{}:{}: dereference of null pointer '{}' (cfire/NULL-DEREF)",
                            path_, line, col, id->name)); !r) return r;
                    } else if (it->second == NullState::MaybeNull) {
                        if (auto r = emit_error(std::format(
                            "{}:{}:{}: dereference of potentially null pointer '{}' without '!= none' guard (cfire/NULL-DEREF)",
                            path_, line, col, id->name)); !r) return r;
                    }
                }
            }
        }
        return {};
    }

    [[nodiscard]] auto check_binary_expr(const BinaryExpr& bin, int line, int col) -> std::expected<void, std::string> {
        if (auto r = check_expr(bin.left); !r) return r;
        if (bin.op == "&&" || bin.op == "||") {
            ++in_invalid_unwrap_context_;
            auto guard = cfire::scope_exit([this] { --in_invalid_unwrap_context_; });
            if (auto r = check_expr(bin.right); !r) return r;
        } else {
            if (auto r = check_expr(bin.right); !r) return r;
        }
        if ((bin.op == "/" || bin.op == "%") && is_const_zero_expr(bin.right)) {
            return emit_error(std::format(
                "{}:{}:{}: division or modulo by constant zero (cfire/DIVIDE-BY-ZERO)",
                path_, line, col));
        }
        return {};
    }

    [[nodiscard]] auto check_member_expr(const MemberExpr& mem, int line, int col) -> std::expected<void, std::string> {
        if (mem.object) {
            if (const auto* obj_id = std::get_if<IdentExpr>(&mem.object->node)) {
                if (auto r = check_stdlib_ns_imported(obj_id->name, mem.object->line > 0 ? mem.object->line : line, mem.object->col > 0 ? mem.object->col : col); !r) return r;
                if (obj_id->name == "os" && !is_ident_in_scope_sema("os") &&
                    (mem.field == "STDIN" || mem.field == "STDOUT" || mem.field == "STDERR")) {
                    std::string lower_fn = mem.field;
                    for (char& c : lower_fn) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    return emit_error(std::format(
                        "{}:{}:{}: 'os.{}' has been replaced by 'os.{}()' returning an RAII 'fs.File' stream handle (cfire/DEPRECATED-API)",
                        path_, line, col, mem.field, lower_fn));
                }
                if (!is_ident_in_scope_sema(obj_id->name) && !is_namespace_ident_sema(obj_id->name) &&
                    cf_.fn_sigs.contains(mem.field)) {
                    return {};
                }
            }
        }
        if (auto r = check_expr(mem.object); !r) return r;
        if (!mem.is_namespace && mem.object) {
            if (const auto* id = std::get_if<IdentExpr>(&mem.object->node)) {
                if (optional_vars_.contains(id->name) && !raw_or_smart_ptr_vars_.contains(id->name) &&
                    (mem.field == "len" || mem.field == "empty" || mem.field == "data")) {
                    return emit_error(std::format(
                        "{}:{}:{}: cannot access '.{}' on optional value '{}' without unwrapping; use 'or' to provide a fallback (cfire/UNWRAPPED-OPTIONAL)",
                        path_, line, col, mem.field, id->name));
                }
                if (fallible_vars_.contains(id->name) && !reported_unhandled_vars_.contains(id->name) &&
                    mem.field != "has_value" && mem.field != "value" && mem.field != "value_or" && mem.field != "error") {
                    return emit_error(std::format(
                        "{}:{}:{}: cannot access '.{}' on fallible result '{}' without unwrapping with 'try' or 'or' (cfire/UNHANDLED-RESULT)",
                        path_, line, col, mem.field, id->name));
                }
                if (scalar_primitive_vars_.contains(id->name) && mem.field == "len") {
                    std::string tname = var_types_.contains(id->name) ? var_types_.at(id->name) : "primitive";
                    return emit_error(std::format(
                        "{}:{}:{}: primitive type '{}' has no member '.{}' (cfire/INVALID-MEMBER)",
                        path_, line, col, tname, mem.field));
                }
                if (ptr_or_smart_vars_.contains(id->name) &&
                    mem.field != "reset" && mem.field != "get" &&
                    mem.field != "has_value" && mem.field != "value_or") {
                    if (auto it = ptr_null_state_.find(id->name); it != ptr_null_state_.end()) {
                        if (it->second == NullState::Null) {
                            return emit_error(std::format(
                                "{}:{}:{}: member access on null pointer '{}' (cfire/NULL-DEREF)",
                                path_, line, col, id->name));
                        }
                        if (it->second == NullState::MaybeNull) {
                            return emit_error(std::format(
                                "{}:{}:{}: member access on potentially null pointer '{}' without '!= none' guard (cfire/NULL-DEREF)",
                                path_, line, col, id->name));
                        }
                    }
                }
            } else if (expr_returns_optional_sema(mem.object) &&
                       mem.field != "has_value" && mem.field != "value" && mem.field != "value_or") {
                return emit_error(std::format(
                    "{}:{}:{}: cannot access '.{}' on optional value without unwrapping; use 'or' to provide a fallback (cfire/UNWRAPPED-OPTIONAL)",
                    path_, line, col, mem.field));
            }
        }
        return {};
    }

    [[nodiscard]] auto check_index_expr(const IndexExpr& idx, int line, int col) -> std::expected<void, std::string> {
        if (auto r = check_expr(idx.object); !r) return r;
        if (auto r = check_expr(idx.index); !r) return r;

        bool is_map_obj = false;
        const IdentExpr* obj_id = idx.object ? std::get_if<IdentExpr>(&idx.object->node) : nullptr;
        if (obj_id) {
            if (optional_vars_.contains(obj_id->name)) {
                return emit_error(std::format(
                    "{}:{}:{}: cannot index into optional value '{}' without unwrapping; use 'or' to provide a fallback (cfire/UNWRAPPED-OPTIONAL)",
                    path_, line, col, obj_id->name));
            }
            if (fallible_vars_.contains(obj_id->name) && !reported_unhandled_vars_.contains(obj_id->name)) {
                return emit_error(std::format(
                    "{}:{}:{}: cannot index into fallible result '{}' without unwrapping with 'try' or 'or' (cfire/UNHANDLED-RESULT)",
                    path_, line, col, obj_id->name));
            }
            if (scalar_primitive_vars_.contains(obj_id->name)) {
                std::string tname = var_types_.contains(obj_id->name) ? var_types_.at(obj_id->name) : "primitive";
                return emit_error(std::format(
                    "{}:{}:{}: cannot index into non-indexable primitive type '{}' (cfire/INVALID-INDEX)",
                    path_, line, col, tname));
            }
            if (map_vars_.contains(obj_id->name)) {
                is_map_obj = true;
            }
        } else if (idx.object && expr_returns_optional_sema(idx.object)) {
            return emit_error(std::format(
                "{}:{}:{}: cannot index into optional value without unwrapping; use 'or' to provide a fallback (cfire/UNWRAPPED-OPTIONAL)",
                path_, line, col));
        }
        if (!is_map_obj && idx.index) {
            if (const auto* un_idx = std::get_if<UnaryExpr>(&idx.index->node)) {
                if (un_idx->op == "-" && un_idx->operand) {
                    if (const auto* neg_lit = std::get_if<LiteralExpr>(&un_idx->operand->node);
                        neg_lit && neg_lit->kind == TokenKind::Int && neg_lit->value != "0") {
                        if (auto r = emit_error(std::format(
                            "{}:{}:{}: negative index -{} is out of bounds (cfire/INDEX-OUT-OF-BOUNDS)",
                            path_, line, col, neg_lit->value)); !r) return r;
                    }
                }
            } else if (const auto* idx_lit = std::get_if<LiteralExpr>(&idx.index->node)) {
                if (idx_lit->kind == TokenKind::Int) {
                    if (idx_lit->value.starts_with('-') && idx_lit->value != "-0") {
                        if (auto r = emit_error(std::format(
                            "{}:{}:{}: negative index {} is out of bounds (cfire/INDEX-OUT-OF-BOUNDS)",
                            path_, line, col, idx_lit->value)); !r) return r;
                    } else if (auto k = cfire::detail::parse_int<std::size_t>(idx_lit->value)) {
                        if (obj_id) {
                            if (auto fit = fixed_array_lens_.find(obj_id->name); fit != fixed_array_lens_.end()) {
                                if (*k >= fit->second) {
                                    if (auto r = emit_error(std::format(
                                        "{}:{}:{}: index {} is out of bounds for array '{}' of length {} (cfire/INDEX-OUT-OF-BOUNDS)",
                                        path_, line, col, *k, obj_id->name, fit->second)); !r) return r;
                                }
                            }
                        } else if (idx.object) {
                            if (const auto* arr_lit = std::get_if<ArrayLitExpr>(&idx.object->node)) {
                                if (*k >= arr_lit->elements.size()) {
                                    if (auto r = emit_error(std::format(
                                        "{}:{}:{}: index {} is out of bounds for array literal of length {} (cfire/INDEX-OUT-OF-BOUNDS)",
                                        path_, line, col, *k, arr_lit->elements.size())); !r) return r;
                                }
                            }
                        }
                    }
                }
            }
        }
        return {};
    }

    [[nodiscard]] auto check_or_expr(const OrExpr& or_ex, int line, int col, const ExprPtr& self_expr = nullptr) -> std::expected<void, std::string> {
        std::size_t errors_before = errors_.size();
        if (auto r = check_expr(or_ex.inner); !r) return r;
        bool is_opt = is_expr_optional_for_unwrap_sema(or_ex.inner);
        bool is_fall = is_expr_fallible_for_unwrap_sema(or_ex.inner);
        if (!or_ex.err_var.empty() && is_opt && !is_fall) {
            if (auto r = emit_error(std::format(
                "{}:{}:{}: cannot bind error variable '{}' on optional 'T?' in 'or' block; use 'or {{ ... }}' or 'or fallback' instead (cfire/OPTIONAL-OR-ERR)",
                path_, line, col, or_ex.err_var)); !r) return r;
        } else if (errors_.size() == errors_before && !has_cpp_imports_ && !seen_cpp_block_ && !is_fall && !is_opt) {
            if (auto r = emit_error(std::format(
                "{}:{}:{}: 'or' used on non-fallible, non-optional expression; remove 'or' fallback (cfire/REDUNDANT-OR)",
                path_, line, col)); !r) return r;
        }
        if (or_ex.fallback_val) {
            if (auto r = check_expr(or_ex.fallback_val); !r) return r;
        }
        if (!or_ex.handler_body.empty()) {
            if (in_invalid_unwrap_context_ > 0) {
                if (auto r = emit_error(std::format(
                    "{}:{}:{}: cannot use block 'or {{ ... }}' inside short-circuit operand or 'for' loop condition; hoist into a statement or use 'or fallback' (cfire/INVALID-OR-BLOCK-CONTEXT)",
                    path_, line, col)); !r) return r;
            }
            if ((!self_expr || !discarded_or_exprs_.contains(self_expr)) && !block_diverges(or_ex.handler_body)) {
                if (auto r = emit_error(std::format(
                    "{}:{}:{}: block 'or {} {{ ... }}' must definitely diverge with return or panic, otherwise unwrapping empty value causes undefined behavior (cfire/NON-DIVERGING-OR-BLOCK)",
                    path_, line, col, or_ex.err_var.empty() ? "err" : or_ex.err_var)); !r) return r;
            }
            ScopeGuard sg(*this);
            if (!or_ex.err_var.empty()) {
                bind_scoped_var(or_ex.err_var, false);
            }
            if (auto r = check_block_stmts(or_ex.handler_body); !r) return r;
        }
        return {};
    }

    [[nodiscard]] auto check_call_expr(const CallExpr& call, int line, int col, const ExprPtr& self_expr = nullptr) -> std::expected<void, std::string> {
        if (auto r = check_expr(call.callee); !r) return r;
        const FnDecl* target_sig = nullptr;
        const StdFnSigInfo* std_sig = nullptr;
        const auto& catalog_idx = StdlibCatalogIndex::instance();

        if (const auto* id = std::get_if<IdentExpr>(&call.callee->node)) {
            if ((id->name == "readln" || id->name == "input") && !is_ident_in_scope_sema(id->name)) {
                if (auto r = emit_error(std::format(
                    "{}:{}:{}: builtin '{}' has been removed; use 'cli.prompt(...)' for interactive prompts or 'os.stdin().read_line()' for stdin streams (cfire/DEPRECATED-API)",
                    path_, line, col, id->name)); !r) return r;
            }
            if ((id->name == "shared" || id->name == "own") && !is_ident_in_scope_sema(id->name)) {
                if (auto r = emit_error(std::format(
                    "{}:{}:{}: cannot call '{}' directly as a constructor; use 'make[{}[T]](...)' instead (cfire/INVALID-SMART-PTR-CTOR)",
                    path_, line, col, id->name, id->name)); !r) return r;
            }
            if (!pkg_name_.empty()) {
                if (auto pit = cf_.fn_sigs.find(pkg_name_ + "." + id->name); pit != cf_.fn_sigs.end()) {
                    target_sig = &pit->second;
                }
            }
            if (!target_sig) {
                if (auto it = cf_.fn_sigs.find(id->name); it != cf_.fn_sigs.end()) {
                    target_sig = &it->second;
                }
            }
        } else if (const auto* mem = std::get_if<MemberExpr>(&call.callee->node)) {
            const auto* obj_id = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
            if (obj_id && obj_id->name == "ai" && !is_ident_in_scope_sema("ai") &&
                (mem->field == "embed" || mem->field == "embed_image" || mem->field == "ocr" || mem->field == "detect_language")) {
                if (auto r = emit_error(std::format(
                    "{}:{}:{}: 'ai.{}' is a stateful Client method; construct a client first (e.g. 'client := ai.local()') and call 'client.{}(...)' (cfire/DEPRECATED-API)",
                    path_, line, col, mem->field, mem->field)); !r) return r;
            }
            if (obj_id && obj_id->name == "fs" && !is_ident_in_scope_sema("fs")) {
                if (mem->field == "read_all") {
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: 'fs.read_all' has been renamed to 'fs.read_bytes' (or 'fs.read_text' for UTF-8 strings) (cfire/DEPRECATED-API)",
                        path_, line, col)); !r) return r;
                } else if (mem->field == "write_all") {
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: 'fs.write_all' has been renamed to 'fs.write_bytes' (or 'fs.write_text' for UTF-8 strings) (cfire/DEPRECATED-API)",
                        path_, line, col)); !r) return r;
                }
            }
            if (obj_id && obj_id->name == "os" && !is_ident_in_scope_sema("os")) {
                if (mem->field == "read_stdin" || mem->field == "read_stdin_bytes" || mem->field == "read_stdin_lines") {
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: 'os.{}' has been replaced by 'os.stdin().read_text()', 'os.stdin().read_bytes()', or 'os.stdin().read_lines()' (cfire/DEPRECATED-API)",
                        path_, line, col, mem->field)); !r) return r;
                } else if (mem->field == "fd_read" || mem->field == "fd_read_bytes" || mem->field == "fd_write" ||
                           mem->field == "fd_close" || mem->field == "poll_fd") {
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: 'os.{}' has been removed; use methods on 'fs.File' ('os.stdin()', 'os.stdout()', 'os.stderr()', 'os.pipe()') or 'net.TcpConn' (cfire/DEPRECATED-API)",
                        path_, line, col, mem->field)); !r) return r;
                } else if (mem->field == "socketpair") {
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: 'os.socketpair' has been moved to 'net.socketpair() -> (net.TcpConn, net.TcpConn)!' (cfire/DEPRECATED-API)",
                        path_, line, col)); !r) return r;
                } else if (mem->field == "is_tty" && !call.args.empty()) {
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: 'os.is_tty(fd)' no longer accepts a raw FD; use 'os.is_tty()' for stdin or call '.is_tty()' on an 'fs.File' handle (e.g. 'os.stdout().is_tty()') (cfire/DEPRECATED-API)",
                        path_, line, col)); !r) return r;
                }
            }
            if (mem->field == "find" || mem->field == "rfind") {
                bool is_str_ns = (obj_id && obj_id->name == "str" && !is_ident_in_scope_sema(obj_id->name));
                if (!is_str_ns && mem->object && infer_expr_type_name_sema(mem->object) == "str") {
                    std::string_view canonical = (mem->field == "rfind") ? "last_index_of" : "index_of";
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: unknown str method '{}'; use '{}' instead (returns i64 index or -1) (cfire/UNKNOWN-METHOD)",
                        path_, line, col, mem->field, canonical)); !r) return r;
                }
            }
            target_sig = resolve_member_call_sig(*mem);
            if (!target_sig && (!obj_id || !catalog_idx.has_namespace(obj_id->name) || is_ident_in_scope_sema(obj_id->name))) {
                if (mem->field == "read_str") {
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: '.read_str' has been renamed to '.read_text' (cfire/DEPRECATED-API)",
                        path_, line, col)); !r) return r;
                } else if (mem->field == "write_str") {
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: '.write_str' has been renamed to '.write_text' (cfire/DEPRECATED-API)",
                        path_, line, col)); !r) return r;
                } else if (mem->field == "sftp_read_all") {
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: '.sftp_read_all' has been renamed to '.sftp_read_text' or '.sftp_read_bytes' (cfire/DEPRECATED-API)",
                        path_, line, col)); !r) return r;
                } else if (mem->field == "sftp_write_all") {
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: '.sftp_write_all' has been renamed to '.sftp_write_text' or '.sftp_write_bytes' (cfire/DEPRECATED-API)",
                        path_, line, col)); !r) return r;
                }
                std::string recv_t = mem->object ? infer_expr_type_name_sema(mem->object) : "";
                bool is_stream_handle = (recv_t == "File" || recv_t == "fs.File" || recv_t == "fs::File" ||
                                         recv_t == "TcpConn" || recv_t == "net.TcpConn" || recv_t == "net::TcpConn" ||
                                         recv_t == "ssh.Channel" || recv_t == "ssh::Channel" ||
                                         (recv_t == "Channel" && is_stdlib_ns_imported("ssh")));
                if (is_stream_handle) {
                    if (mem->field == "read_all") {
                        if (auto r = emit_error(std::format(
                            "{}:{}:{}: '.read_all' on File has been replaced by '.read_bytes()' or '.read_text()' (cfire/DEPRECATED-API)",
                            path_, line, col)); !r) return r;
                    } else if (mem->field == "read") {
                        if (auto r = emit_error(std::format(
                            "{}:{}:{}: '.read' has been replaced by explicit '.read_bytes', '.read_text', or '.read_into' (cfire/DEPRECATED-API)",
                            path_, line, col)); !r) return r;
                    } else if (mem->field == "write") {
                        if (auto r = emit_error(std::format(
                            "{}:{}:{}: '.write' has been replaced by explicit '.write_bytes' or '.write_text' (cfire/DEPRECATED-API)",
                            path_, line, col)); !r) return r;
                    }
                }
            }
            if (!target_sig && obj_id) {
                if (catalog_idx.has_namespace(obj_id->name) && !is_ident_in_scope_sema(obj_id->name)) {
                    std_sig = catalog_idx.find_ns_fn(obj_id->name, mem->field);
                    if (!std_sig && obj_id->name != "rl" && obj_id->name != "rg" &&
                        !catalog_idx.is_known_ns_member(obj_id->name, mem->field)) {
                        std::string sugg = catalog_idx.suggest_closest_fn(obj_id->name, mem->field);
                        std::string hint = sugg.empty() ? "" : std::format("; did you mean '{}.{}'?", obj_id->name, sugg);
                        if (auto r = emit_error(std::format(
                            "{}:{}:{}: unknown function '{}.{}' in module '{}'{} (cfire/UNKNOWN-STDLIB-FN)",
                            path_, line, col, obj_id->name, mem->field, catalog_idx.ns_to_import.at(obj_id->name), hint)); !r) return r;
                    }
                } else if (auto dot = obj_id->name.find('.'); dot != std::string::npos) {
                    std::string ns_part = obj_id->name.substr(0, dot);
                    std::string type_part = obj_id->name.substr(dot + 1);
                    if (catalog_idx.has_namespace(ns_part) && !is_ident_in_scope_sema(ns_part)) {
                        std_sig = catalog_idx.find_ns_fn(ns_part, type_part + "." + mem->field);
                    }
                }
            }
            if (!target_sig && !std_sig && mem->object) {
                std::string recv_t = infer_expr_type_name_sema(mem->object);
                if (recv_t == "File" || recv_t == "fs.File" || recv_t == "fs::File" ||
                    recv_t == "TcpConn" || recv_t == "net.TcpConn" || recv_t == "net::TcpConn") {
                    std::string norm_t = (recv_t.ends_with("File")) ? "File" : "TcpConn";
                    if (auto mit = catalog_idx.receiver_methods.find(norm_t + "." + mem->field); mit != catalog_idx.receiver_methods.end()) {
                        std_sig = &mit->second;
                    }
                }
            }
            if (target_sig && target_sig->receiver && target_sig->receiver->intent == "mut") {
                if (obj_id && imm_vars_.contains(obj_id->name) && !smart_ptr_vars_.contains(obj_id->name)) {
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: cannot call mutating method '{}' on immutable variable '{}' (cfire/MUT-RECEIVER)",
                        path_, line, col, mem->field, obj_id->name)); !r) return r;
                }
            }
            if (mem->field == "push" && !call.args.empty() && is_byte_slice_expr(mem->object)) {
                if (is_non_byte_int_or_u8_expr(call.args[0].value)) {
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: cannot push integer/u8 value into '[]byte' without explicit 'byte(...)' conversion (cfire/TYPE-MISMATCH)",
                        path_, call.args[0].line, call.args[0].col)); !r) return r;
                }
            }
            if (mem->field == "push" || mem->field == "pop" || mem->field == "clear" ||
                mem->field == "erase" || mem->field == "remove") {
                if (auto pi = extract_path_info(mem->object)) {
                    if (auto r = check_not_iterated_collection(pi->root, line, col); !r) return r;
                    invalidate_loans_for_owner(
                        pi->root,
                        std::format("underlying container '{}' was mutated at line {}", pi->root, line),
                        line);
                }
            }
            if (!call.args.empty() && is_dynamic_string_expr(call.args[0].value)) {
                const auto* recv_id = mem->object ? std::get_if<IdentExpr>(&mem->object->node) : nullptr;
                if (recv_id && recv_id->name == "os" && (mem->field == "exec" || mem->field == "exec_output")) {
                    warnings_.push_back(std::format(
                        "{}:{}:{}: warning: dynamic string interpolation/concatenation in 'os.{}' risks shell command injection; prefer 'os.command(binary, args)' (cfire/SHELL-INJECTION-RISK)",
                        path_, call.args[0].line, call.args[0].col, mem->field));
                } else if ((!recv_id || recv_id->name != "os") && (mem->field == "exec" || mem->field == "query")) {
                    warnings_.push_back(std::format(
                        "{}:{}:{}: warning: dynamic string interpolation/concatenation in SQL '{}' risks SQL injection; use parameterized '?1' bindings instead (cfire/SQL-INJECTION-RISK)",
                        path_, call.args[0].line, call.args[0].col, mem->field));
                }
            }
        }

        if (target_sig) {
            std::size_t actual_args = call.args.size() + (call.block_arg ? 1 : 0);
            if (actual_args != target_sig->params.size()) {
                if (auto r = emit_error(std::format(
                    "{}:{}:{}: call to '{}' expects {} argument(s), got {} (cfire/ARITY-MISMATCH)",
                    path_, line, col, target_sig->name, target_sig->params.size(), actual_args)); !r) return r;
            }
        } else if (std_sig) {
            std::size_t actual_args = call.args.size() + (call.block_arg ? 1 : 0);
            if (actual_args < std_sig->min_args || actual_args > std_sig->max_args) {
                std::string exp_str = (std_sig->min_args == std_sig->max_args)
                    ? std::format("{}", std_sig->min_args)
                    : (std_sig->max_args >= 512
                        ? std::format("at least {}", std_sig->min_args)
                        : std::format("{}..{}", std_sig->min_args, std_sig->max_args));
                if (auto r = emit_error(std::format(
                    "{}:{}:{}: call to '{}.{}' expects {} argument(s), got {} (cfire/ARITY-MISMATCH)",
                    path_, line, col, std_sig->ns_alias, std_sig->name, exp_str, actual_args)); !r) return r;
            }
        }

        if (self_expr && !unwrapped_exprs_.contains(self_expr)) {
            std::string fallible_callee;
            std::string fallible_ret;
            if (expr_returns_fallible_sema(self_expr, &fallible_callee, &fallible_ret)) {
                auto [start_ln, start_col] = expr_start_loc(self_expr);
                if (auto r = emit_error(std::format(
                    "{}:{}:{}: fallible call to '{}' returns '{}' and must be handled with 'try' or 'or' (cfire/UNHANDLED-RESULT)",
                    path_, start_ln, start_col, fallible_callee, fallible_ret)); !r) return r;
            }
        }

        struct SeenArgPath {
            PathInfo info;
            bool is_mut_or_out;
        };
        std::vector<SeenArgPath> seen_paths;

        if (const auto* mem = std::get_if<MemberExpr>(&call.callee->node)) {
            if (auto p = extract_path_info(mem->object)) {
                bool is_mut = (target_sig && target_sig->receiver && target_sig->receiver->intent == "mut");
                seen_paths.push_back({*p, is_mut});
            }
        }

        for (std::size_t i = 0; i < call.args.size(); ++i) {
            const auto& arg = call.args[i];
            if (arg.intent == "out") {
                warnings_.push_back(std::format(
                    "{}:{}:{}: warning: 'out' call-site annotation is deprecated; use multi-return tuple destructuring instead (cfire/DEPRECATED-OUT-PARAM)",
                    path_, arg.line, arg.col));
            }
            if (auto r = check_expr(arg.value); !r) return r;
            bool expected_is_move = false;
            if (target_sig && i < target_sig->params.size()) {
                const auto& expected = target_sig->params[i].intent;
                expected_is_move = (expected == "move");
                if ((expected == "mut" || expected == "out" || expected == "move") && arg.intent != expected) {
                    std::string_view rule = (expected == "out")
                        ? "cfire/OUT-CALLSITE"
                        : (expected == "move" ? "cfire/MOVE-CALLSITE" : "cfire/MUT-CALLSITE");
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: parameter '{}' of {} requires explicit '{}' annotation at call-site ({})",
                        path_, arg.line, arg.col, target_sig->params[i].name, target_sig->name, expected, rule)); !r) return r;
                } else if (expected == "in" && (arg.intent == "mut" || arg.intent == "out")) {
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: parameter '{}' of '{}' has 'in' intent; remove redundant '{}' annotation at call-site (cfire/REDUNDANT-CALLSITE-INTENT)",
                        path_, arg.line, arg.col, target_sig->params[i].name, target_sig->name, arg.intent)); !r) return r;
                }
                const auto& pt = target_sig->params[i].type;
                if (pt.name == "str" && !pt.is_ptr && !pt.is_mut_ptr && !pt.is_slice && !pt.is_array && is_str_slice_expr(arg.value)) {
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: cannot pass zero-copy '&str' slice view to owning 'str' parameter '{}' of '{}'; change parameter type to '&str' or convert explicitly with 'str(...)' (cfire/TYPE-MISMATCH)",
                        path_, arg.line, arg.col, target_sig->params[i].name, target_sig->name)); !r) return r;
                }
                if ((pt.is_slice || pt.is_array) && pt.name == "byte") {
                    if (const auto* arr_lit = std::get_if<ArrayLitExpr>(&arg.value->node)) {
                        for (const auto& el : arr_lit->elements) {
                            if (is_non_byte_int_or_u8_expr(el)) {
                                if (auto r = emit_error(std::format(
                                    "{}:{}:{}: cannot use integer/u8 value in '[]byte' literal without explicit 'byte(...)' conversion (cfire/TYPE-MISMATCH)",
                                    path_, el->line, el->col)); !r) return r;
                            }
                        }
                    }
                }
            } else if (std_sig && i < std_sig->params.size()) {
                const auto& expected = std_sig->params[i].intent;
                expected_is_move = (expected == "move");
                if ((expected == "mut" || expected == "out" || expected == "move") && arg.intent != expected) {
                    std::string_view rule = (expected == "out")
                        ? "cfire/OUT-CALLSITE"
                        : (expected == "move" ? "cfire/MOVE-CALLSITE" : "cfire/MUT-CALLSITE");
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: parameter '{}' of {}.{} requires explicit '{}' annotation at call-site ({})",
                        path_, arg.line, arg.col, std_sig->params[i].name, std_sig->ns_alias, std_sig->name, expected, rule)); !r) return r;
                } else if (expected == "in" && (arg.intent == "mut" || arg.intent == "out")) {
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: parameter '{}' of '{}.{}' has 'in' intent; remove redundant '{}' annotation at call-site (cfire/REDUNDANT-CALLSITE-INTENT)",
                        path_, arg.line, arg.col, std_sig->params[i].name, std_sig->ns_alias, std_sig->name, arg.intent)); !r) return r;
                }
            } else if ((arg.intent == "mut" || arg.intent == "out") && call.callee) {
                if (const auto* cid = std::get_if<IdentExpr>(&call.callee->node)) {
                    if (!is_ident_in_scope_sema(cid->name) &&
                        (cid->name == "print" || cid->name == "println" || cid->name == "assert" ||
                         cid->name == "len" || cid->name == "cap" || cid->name == "str" ||
                         cid->name == "int" || cid->name == "i8" || cid->name == "i16" ||
                         cid->name == "i32" || cid->name == "i64" || cid->name == "isize" ||
                         cid->name == "u8" || cid->name == "u16" || cid->name == "u32" ||
                         cid->name == "u64" || cid->name == "usize" || cid->name == "byte" ||
                         cid->name == "f32" || cid->name == "f64" || cid->name == "bool" ||
                         cid->name == "make" || cid->name == "panic" || cid->name == "exit")) {
                        if (auto r = emit_error(std::format(
                            "{}:{}:{}: builtin '{}' takes read-only 'in' arguments; remove redundant '{}' annotation at call-site (cfire/REDUNDANT-CALLSITE-INTENT)",
                            path_, arg.line, arg.col, cid->name, arg.intent)); !r) return r;
                    }
                }
            }
            if (arg.intent == "move") {
                if (auto r = validate_move_operand(arg.value, expected_is_move, arg.line, arg.col); !r) return r;
            }
            if (auto pi = extract_path_info(arg.value)) {
                bool is_mut_or_out = (arg.intent == "mut" || arg.intent == "out");
                for (const auto& prev : seen_paths) {
                    if (prev.info.root == pi->root && (prev.is_mut_or_out || is_mut_or_out)) {
                        if (paths_conflict(prev.info.path, pi->path)) {
                            std::string entity = (pi->path == pi->root && prev.info.path == prev.info.root)
                                ? std::format("variable '{}'", pi->root)
                                : std::format("projection '{}'", pi->path);
                            if (auto r = emit_error(std::format(
                                "{}:{}:{}: cannot alias {} with a mut/out parameter in the same call (cfire/MUT-ALIAS)",
                                path_, arg.line, arg.col, entity)); !r) return r;
                        }
                    }
                }
                seen_paths.push_back({*pi, is_mut_or_out});

                if (is_mut_or_out) {
                    if (imm_vars_.contains(pi->root)) {
                        std::string entity = (pi->path == pi->root) ? pi->root : pi->path;
                        if (auto r = emit_error(std::format(
                            "{}:{}:{}: cannot pass immutable variable '{}' as '{}' argument (cfire/IMMUTABLE-MUT-ARG)",
                            path_, arg.line, arg.col, entity, arg.intent)); !r) return r;
                    }
                    if (pi->path == pi->root) {
                        if (auto r = check_not_iterated_collection(pi->root, arg.line, arg.col); !r) return r;
                    }
                }
                if (arg.intent == "move") {
                    if (auto r = check_not_iterated_collection(pi->root, arg.line, arg.col); !r) return r;
                    moved_at_line_[pi->root] = arg.line;
                    invalidate_loans_for_owner(
                        pi->root,
                        std::format("underlying owner '{}' was moved at line {}", pi->root, arg.line),
                        arg.line);
                }
            }
        }
        if (const auto* id = std::get_if<IdentExpr>(&call.callee->node)) {
            if (id->name == "assert" && !call.args.empty()) {
                if (auto nc = extract_null_check(call.args[0].value); nc && nc->second) {
                    ptr_null_state_[nc->first] = NullState::NonNull;
                }
            }
        }
        if (call.block_arg) {
            ++in_closure_depth_;
            int saved_loop_depth = loop_depth_;
            int saved_invalid_unwrap = in_invalid_unwrap_context_;
            loop_depth_ = 0;
            in_invalid_unwrap_context_ = 0;
            auto cl_guard = cfire::scope_exit([this, saved_loop_depth, saved_invalid_unwrap] {
                --in_closure_depth_;
                loop_depth_ = saved_loop_depth;
                in_invalid_unwrap_context_ = saved_invalid_unwrap;
            });
            ScopeGuard sg(*this);
            for (std::size_t i = 0; i < call.block_arg->params.size(); ++i) {
                bool is_mut = (i < call.block_arg->param_intents.size()) &&
                              (call.block_arg->param_intents[i] == "mut" || call.block_arg->param_intents[i] == "out");
                bind_scoped_var(call.block_arg->params[i], is_mut);
            }
            if (auto r = check_block_stmts(call.block_arg->body); !r) return r;
        }
        return {};
    }

    [[nodiscard]] auto check_match_common(
        const ExprPtr& target,
        bool is_move,
        const std::vector<MatchArm>& arms,
        int line,
        int col,
        bool is_expr_match
    ) -> std::expected<void, std::string> {
        if (auto r = check_expr(target); !r) return r;
        if (is_move && target) {
            if (auto r = validate_move_operand(target, false, line, col); !r) return r;
            if (auto pi = extract_path_info(target)) {
                if (auto r = check_not_iterated_collection(pi->root, line, col); !r) return r;
                moved_at_line_[pi->root] = line;
                invalidate_loans_for_owner(
                    pi->root,
                    std::format("underlying owner '{}' was moved at line {}", pi->root, line),
                    line);
            }
        }
        if (auto r = check_match_arms_reachability(arms, line, col); !r) return r;
        bool is_slice_match = false;
        bool is_primitive_match = false;
        for (const auto& a : arms) {
            if (a.is_slice_pattern) is_slice_match = true;
            if (!a.literal_val.empty()) is_primitive_match = true;
        }
        if (!is_expr_match && is_slice_match) {
            for (const auto& arm : arms) {
                ScopeGuard sg(*this);
                bind_match_arm_vars(arm);
                if (auto r = check_block_stmts(arm.body); !r) return r;
            }
            return {};
        }

        std::string enum_name;
        std::unordered_set<std::string> covered;
        bool has_wildcard = false;

        for (const auto& arm : arms) {
            if (arm.variant_name == "_") {
                has_wildcard = true;
            } else {
                covered.insert(arm.variant_name);
                if (!arm.enum_prefix.empty()) {
                    enum_name = arm.enum_prefix;
                } else if (auto vit = cf_.variant_to_enum.find(arm.variant_name); vit != cf_.variant_to_enum.end()) {
                    enum_name = vit->second;
                }
            }
            ScopeGuard sg(*this);
            bind_match_arm_vars(arm);
            if (auto r = check_block_stmts(arm.body, is_expr_match); !r) return r;
        }

        if (!has_wildcard && !enum_name.empty()) {
            if (auto eit = cf_.enums.find(enum_name); eit != cf_.enums.end()) {
                for (const auto& v : eit->second.variants) {
                    if (!covered.contains(v.name)) {
                        if (auto r = emit_error(std::format(
                            "{}:{}:{}: non-exhaustive match on enum '{}': missing variant '{}' (cfire/NON-EXHAUSTIVE-MATCH)",
                            path_, line, col, enum_name, v.name)); !r) return r;
                    }
                }
            }
        } else if (is_expr_match && (is_slice_match || is_primitive_match || enum_name.empty()) && !has_wildcard) {
            if (!is_slice_match || !is_slice_exhaustively_covered(arms)) {
                if (auto r = emit_error(std::format(
                    "{}:{}:{}: match expression must have a wildcard '_' fallback arm (cfire/NON-EXHAUSTIVE-MATCH)",
                    path_, line, col)); !r) return r;
            }
        }
        return {};
    }

    [[nodiscard]] static auto is_slice_exhaustively_covered(const std::vector<MatchArm>& arms) -> bool {
        std::unordered_set<std::size_t> exact_covered;
        std::optional<std::size_t> min_variadic_covered;
        for (const auto& arm : arms) {
            if (arm.variant_name == "_" && !arm.is_slice_pattern && arm.literal_val.empty()) {
                return true;
            }
            if (arm.is_slice_pattern) {
                std::size_t min_sz = arm.slice_head.size() + arm.slice_tail.size();
                if (!arm.slice_has_rest) {
                    exact_covered.insert(min_sz);
                } else {
                    if (!min_variadic_covered || min_sz < *min_variadic_covered) {
                        min_variadic_covered = min_sz;
                    }
                }
            }
        }
        if (!min_variadic_covered) return false;
        for (std::size_t k = 0; k < *min_variadic_covered; ++k) {
            if (!exact_covered.contains(k)) return false;
        }
        return true;
    }

    [[nodiscard]] auto check_match_arms_reachability(
        const std::vector<MatchArm>& arms,
        int match_line,
        int match_col
    ) -> std::expected<void, std::string> {
        bool seen_wildcard = false;
        std::unordered_set<std::string> seen_variants;
        std::unordered_set<std::string> seen_literals;
        std::unordered_set<std::size_t> exact_slice_sizes;
        std::optional<std::size_t> min_variadic_slice;

        auto is_size_covered = [&](std::size_t n) -> bool {
            if (exact_slice_sizes.contains(n)) return true;
            if (min_variadic_slice && n >= *min_variadic_slice) return true;
            return false;
        };

        auto all_sizes_from_covered = [&](std::size_t m) -> bool {
            if (!min_variadic_slice) return false;
            for (std::size_t k = m; k < *min_variadic_slice; ++k) {
                if (!exact_slice_sizes.contains(k)) return false;
            }
            return true;
        };

        for (std::size_t idx = 0; idx < arms.size(); ++idx) {
            const auto& arm = arms[idx];
            if (seen_wildcard) {
                return emit_error(std::format(
                    "{}:{}:{}: unreachable match arm #{}: previous wildcard '_' arm already matches all remaining cases (cfire/UNREACHABLE-MATCH-ARM)",
                    path_, match_line, match_col, idx + 1));
            }
            if (arm.is_slice_pattern) {
                std::size_t min_sz = arm.slice_head.size() + arm.slice_tail.size();
                if (!arm.slice_has_rest) {
                    if (is_size_covered(min_sz)) {
                        return emit_error(std::format(
                            "{}:{}:{}: unreachable slice match arm #{} (exact length {} is already matched by an earlier arm) (cfire/UNREACHABLE-MATCH-ARM)",
                            path_, match_line, match_col, idx + 1, min_sz));
                    }
                    exact_slice_sizes.insert(min_sz);
                } else {
                    if (all_sizes_from_covered(min_sz)) {
                        return emit_error(std::format(
                            "{}:{}:{}: unreachable slice match arm #{} (slices of length >= {} are already matched by earlier arms) (cfire/UNREACHABLE-MATCH-ARM)",
                            path_, match_line, match_col, idx + 1, min_sz));
                    }
                    if (!min_variadic_slice || min_sz < *min_variadic_slice) {
                        min_variadic_slice = min_sz;
                    }
                }
                continue;
            }
            if (arm.variant_name == "_" && arm.literal_val.empty()) {
                if (min_variadic_slice && all_sizes_from_covered(0)) {
                    return emit_error(std::format(
                        "{}:{}:{}: unreachable wildcard '_' match arm #{}: all slice lengths are already covered by earlier arms (cfire/UNREACHABLE-MATCH-ARM)",
                        path_, match_line, match_col, idx + 1));
                }
                seen_wildcard = true;
                continue;
            }
            if (!arm.literal_val.empty()) {
                if (seen_literals.contains(arm.literal_val)) {
                    return emit_error(std::format(
                        "{}:{}:{}: unreachable match arm #{}: duplicate literal pattern '{}' (cfire/UNREACHABLE-MATCH-ARM)",
                        path_, match_line, match_col, idx + 1, arm.literal_val));
                }
                seen_literals.insert(arm.literal_val);
                continue;
            }
            if (!arm.variant_name.empty()) {
                if (seen_variants.contains(arm.variant_name)) {
                    return emit_error(std::format(
                        "{}:{}:{}: unreachable match arm #{}: duplicate variant pattern '{}' (cfire/UNREACHABLE-MATCH-ARM)",
                        path_, match_line, match_col, idx + 1, arm.variant_name));
                }
                seen_variants.insert(arm.variant_name);
            }
        }
        return {};
    }

    [[nodiscard]] static auto stmt_diverges(const StmtPtr& s) -> bool {
        if (!s) return false;
        if (std::holds_alternative<ReturnStmt>(s->node)) return true;
        if (const auto* es = std::get_if<ExprStmt>(&s->node)) {
            if (es->expr) {
                if (const auto* id = std::get_if<IdentExpr>(&es->expr->node)) {
                    if (id->name == "break" || id->name == "continue") return true;
                }
                if (const auto* call = std::get_if<CallExpr>(&es->expr->node)) {
                    if (const auto* id = std::get_if<IdentExpr>(&call->callee->node)) {
                        if (id->name == "panic" || id->name == "exit") return true;
                    } else if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                        if (mem->field == "exit" && mem->object) {
                            if (const auto* obj_id = std::get_if<IdentExpr>(&mem->object->node)) {
                                if (obj_id->name == "os") return true;
                            }
                        }
                    }
                }
            }
        }
        if (const auto* ifs = std::get_if<IfStmt>(&s->node)) {
            if (!ifs->else_body.empty() && block_diverges(ifs->then_body) && block_diverges(ifs->else_body)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] static auto block_diverges(const std::vector<StmtPtr>& body) -> bool {
        for (const auto& s : body) {
            if (stmt_diverges(s)) return true;
        }
        return false;
    }

    struct SavedVarDeclState {
        std::optional<int> scope_depth{};
        bool was_local_defined{false};
        bool was_mut{false};
        bool was_imm{false};
        std::optional<std::string> var_type{};
        bool was_optional{false};
        bool was_fallible{false};
        bool was_reported_unhandled{false};
        bool was_scalar_primitive{false};
        bool was_ptr_or_smart{false};
        bool was_raw_or_smart_ptr{false};
        std::optional<NullState> null_state{};
        std::optional<std::size_t> fixed_array_len{};
        bool was_map{false};
        std::optional<std::string> borrow_origin{};
        std::optional<int> moved_at_line{};
        std::optional<InvalidatedLoan> invalidated_loan{};
    };

    struct ScopeGuard {
        SemaFnChecker& checker;
        std::unordered_map<std::string, SavedVarDeclState> saved_vars{};

        explicit ScopeGuard(SemaFnChecker& c) : checker(c) {
            ++checker.scope_depth_;
            checker.scope_stack_.push_back(&saved_vars);
        }
        ~ScopeGuard() {
            for (const auto& [name, st] : saved_vars) {
                if (st.scope_depth) checker.var_scope_depth_[name] = *st.scope_depth;
                else checker.var_scope_depth_.erase(name);

                if (st.was_local_defined) checker.local_defined_vars_.insert(name);
                else checker.local_defined_vars_.erase(name);

                if (st.was_mut) checker.mut_vars_.insert(name);
                else checker.mut_vars_.erase(name);

                if (st.was_imm) checker.imm_vars_.insert(name);
                else checker.imm_vars_.erase(name);

                if (st.var_type) checker.var_types_[name] = *st.var_type;
                else checker.var_types_.erase(name);

                if (st.was_optional) checker.optional_vars_.insert(name);
                else checker.optional_vars_.erase(name);

                if (st.was_fallible) checker.fallible_vars_.insert(name);
                else checker.fallible_vars_.erase(name);

                if (st.was_reported_unhandled) checker.reported_unhandled_vars_.insert(name);
                else checker.reported_unhandled_vars_.erase(name);

                if (st.was_scalar_primitive) checker.scalar_primitive_vars_.insert(name);
                else checker.scalar_primitive_vars_.erase(name);

                if (st.was_ptr_or_smart) checker.ptr_or_smart_vars_.insert(name);
                else checker.ptr_or_smart_vars_.erase(name);

                if (st.was_raw_or_smart_ptr) checker.raw_or_smart_ptr_vars_.insert(name);
                else checker.raw_or_smart_ptr_vars_.erase(name);

                if (st.null_state) checker.ptr_null_state_[name] = *st.null_state;
                else checker.ptr_null_state_.erase(name);

                if (st.fixed_array_len) checker.fixed_array_lens_[name] = *st.fixed_array_len;
                else checker.fixed_array_lens_.erase(name);

                if (st.was_map) checker.map_vars_.insert(name);
                else checker.map_vars_.erase(name);

                if (st.borrow_origin) checker.borrow_origin_[name] = *st.borrow_origin;
                else checker.borrow_origin_.erase(name);

                if (st.moved_at_line) checker.moved_at_line_[name] = *st.moved_at_line;
                else checker.moved_at_line_.erase(name);

                if (st.invalidated_loan) checker.invalidated_loans_[name] = *st.invalidated_loan;
                else checker.invalidated_loans_.erase(name);
            }
            checker.scope_stack_.pop_back();
            --checker.scope_depth_;
        }
        ScopeGuard(const ScopeGuard&) = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;
    };

    void save_var_before_redecl(const std::string& name) {
        if (scope_stack_.empty() || name.empty() || name == "_") return;
        auto* top = scope_stack_.back();
        if (top->contains(name)) return;
        SavedVarDeclState st;
        if (auto it = var_scope_depth_.find(name); it != var_scope_depth_.end()) st.scope_depth = it->second;
        st.was_local_defined = local_defined_vars_.contains(name);
        st.was_mut = mut_vars_.contains(name);
        st.was_imm = imm_vars_.contains(name);
        if (auto it = var_types_.find(name); it != var_types_.end()) st.var_type = it->second;
        st.was_optional = optional_vars_.contains(name);
        st.was_fallible = fallible_vars_.contains(name);
        st.was_reported_unhandled = reported_unhandled_vars_.contains(name);
        st.was_scalar_primitive = scalar_primitive_vars_.contains(name);
        st.was_ptr_or_smart = ptr_or_smart_vars_.contains(name);
        st.was_raw_or_smart_ptr = raw_or_smart_ptr_vars_.contains(name);
        if (auto it = ptr_null_state_.find(name); it != ptr_null_state_.end()) st.null_state = it->second;
        if (auto it = fixed_array_lens_.find(name); it != fixed_array_lens_.end()) st.fixed_array_len = it->second;
        st.was_map = map_vars_.contains(name);
        if (auto it = borrow_origin_.find(name); it != borrow_origin_.end()) st.borrow_origin = it->second;
        if (auto it = moved_at_line_.find(name); it != moved_at_line_.end()) st.moved_at_line = it->second;
        if (auto it = invalidated_loans_.find(name); it != invalidated_loans_.end()) st.invalidated_loan = it->second;
        top->emplace(name, std::move(st));
    }

    void bind_scoped_var(const std::string& name, bool is_mut) {
        if (name.empty() || name == "_") return;
        save_var_before_redecl(name);
        var_scope_depth_[name] = scope_depth_;
        local_defined_vars_.insert(name);
        moved_at_line_.erase(name);
        invalidated_loans_.erase(name);
        borrow_origin_.erase(name);
        if (is_mut) {
            imm_vars_.erase(name);
            mut_vars_.insert(name);
        } else {
            mut_vars_.erase(name);
            imm_vars_.insert(name);
        }
    }

    void bind_match_arm_vars(const MatchArm& arm) {
        for (const auto& b : arm.bindings) {
            bind_scoped_var(b, false);
        }
        if (arm.is_slice_pattern) {
            for (const auto& h : arm.slice_head) bind_scoped_var(h, false);
            if (!arm.slice_rest.empty()) bind_scoped_var(arm.slice_rest, false);
            for (const auto& t : arm.slice_tail) bind_scoped_var(t, false);
        }
    }

    [[nodiscard]] auto check_block_stmts(const std::vector<StmtPtr>& stmts, bool tail_is_value = false) -> std::expected<void, std::string> {
        for (std::size_t i = 0; i < stmts.size(); ++i) {
            if (tail_is_value && i + 1 == stmts.size() && stmts[i]) {
                if (const auto* es = std::get_if<ExprStmt>(&stmts[i]->node)) {
                    if (auto r = check_expr(es->expr); !r) return r;
                    continue;
                }
            }
            if (auto r = check_stmt(stmts[i]); !r) return r;
        }
        return {};
    }

    [[nodiscard]] auto check_stmt(const StmtPtr& s) -> std::expected<void, std::string> {
        if (!s) return {};
        if (std::holds_alternative<CppBlockStmt>(s->node)) {
            seen_cpp_block_ = true;
            return {};
        }
        if (const auto* vd = std::get_if<VarDeclStmt>(&s->node)) {
            return check_var_decl_stmt(*vd);
        }
        if (const auto* dvs = std::get_if<DestructureVarDeclStmt>(&s->node)) {
            return check_destructure_var_decl_stmt(*dvs);
        }
        if (const auto* as = std::get_if<AssignStmt>(&s->node)) {
            return check_assign_stmt(*as);
        }
        if (const auto* das = std::get_if<DestructureAssignStmt>(&s->node)) {
            return check_destructure_assign_stmt(*das);
        }
        if (const auto* df = std::get_if<DeferStmt>(&s->node)) {
            return check_defer_stmt(*df);
        }
        if (const auto* rt = std::get_if<ReturnStmt>(&s->node)) {
            return check_return_stmt(*rt);
        }
        if (const auto* ifs = std::get_if<IfStmt>(&s->node)) {
            return check_if_stmt(*ifs);
        }
        if (const auto* fs = std::get_if<ForStmt>(&s->node)) {
            return check_for_stmt(*fs);
        }
        if (const auto* ms = std::get_if<MatchStmt>(&s->node)) {
            return check_match_common(ms->target, ms->is_move, ms->arms, ms->line, ms->col, false);
        }
        if (const auto* sc = std::get_if<AsyncScopeStmt>(&s->node)) {
            return check_async_scope_stmt(*sc);
        }
        if (const auto* es = std::get_if<ExprStmt>(&s->node)) {
            if (es->expr) {
                if (const auto* or_ex = std::get_if<OrExpr>(&es->expr->node); or_ex && !or_ex->handler_body.empty()) {
                    discarded_or_exprs_.insert(es->expr);
                }
            }
            std::size_t errors_before = errors_.size();
            if (auto r = check_expr(es->expr); !r) return r;
            if (errors_.size() == errors_before && es->expr && !is_effectful_stmt_expr(es->expr)) {
                auto [ln, col] = expr_start_loc(es->expr);
                if (ln <= 0) ln = s->line;
                if (const auto* bin = std::get_if<BinaryExpr>(&es->expr->node); bin && bin->op == "==") {
                    return emit_error(std::format(
                        "{}:{}:{}: equality comparison '==' statement has no effect; did you mean '=' assignment? (cfire/NO-EFFECT-STMT)",
                        path_, ln, col));
                }
                return emit_error(std::format(
                    "{}:{}:{}: expression statement has no effect; assign the result or call a function (cfire/NO-EFFECT-STMT)",
                    path_, ln, col));
            }
            return {};
        }
        return {};
    }

    [[nodiscard]] auto check_var_decl_stmt(const VarDeclStmt& vd) -> std::expected<void, std::string> {
        if (vd.was_var) {
            warnings_.push_back(std::format(
                "{}:{}:{}: warning: 'var' is deprecated; use 'x := val' or 'mut x: T = val' instead (cfire/DEPRECATED-VAR)",
                path_, vd.line, vd.col));
        }
        if (vd.init && (vd.name == "_" || (vd.type && vd.type->is_fallible))) {
            unwrapped_exprs_.insert(vd.init);
        }
        // Check initializer expression before binding the new variable so `x := x + 1` reads outer `x` first
        if (auto r = check_expr(vd.init); !r) return r;

        bool is_byte_slice = (vd.type && (vd.type->is_slice || vd.type->is_array) &&
                              (vd.type->name == "byte" || (vd.type->elem_type && vd.type->elem_type->name == "byte"))) ||
                             (!vd.type && is_byte_slice_expr(vd.init));
        if (is_byte_slice && vd.init) {
            if (const auto* arr_lit = std::get_if<ArrayLitExpr>(&vd.init->node)) {
                for (const auto& el : arr_lit->elements) {
                    if (is_non_byte_int_or_u8_expr(el)) {
                        if (auto r = emit_error(std::format(
                            "{}:{}:{}: cannot use integer/u8 value in '[]byte' literal without explicit 'byte(...)' conversion (cfire/TYPE-MISMATCH)",
                            path_, el->line, el->col)); !r) return r;
                    }
                }
            }
        }

        auto src = extract_borrow_source(vd.init);
        std::string inferred_t;
        if (vd.type && !vd.type->smart_wrap.empty() && vd.type->elem_type && !vd.type->elem_type->name.empty()) {
            inferred_t = vd.type->elem_type->name;
        } else if (!vd.type || vd.type->name.empty()) {
            inferred_t = infer_expr_type_name_sema(vd.init);
        } else {
            inferred_t = vd.type->name;
        }
        bool is_opt_var = (vd.type && vd.type->is_optional) || (!vd.type && expr_returns_optional_sema(vd.init));
        bool is_fallible_var = (vd.type && vd.type->is_fallible);
        bool init_was_unhandled_fallible = (!vd.type && vd.name != "_" && expr_returns_fallible_sema(vd.init));
        bool is_smart = (vd.type && !vd.type->smart_wrap.empty());
        if (!is_smart && vd.init) {
            const Expr* cur_init = vd.init;
            if (const auto* tr = std::get_if<TryExpr>(&cur_init->node)) cur_init = tr->inner;
            else if (const auto* or_ex = std::get_if<OrExpr>(&cur_init->node)) cur_init = or_ex->inner;
            if (cur_init) {
                if (const auto* call_ex = std::get_if<CallExpr>(&cur_init->node)) {
                    if (const auto* cid = std::get_if<IdentExpr>(&call_ex->callee->node)) {
                        if (cid->name == "make" && call_ex->type_args.size() == 1 && !call_ex->type_args[0].smart_wrap.empty()) {
                            is_smart = true;
                        } else if (auto fit = cf_.fn_sigs.find(cid->name); fit != cf_.fn_sigs.end() && !fit->second.return_type.smart_wrap.empty()) {
                            is_smart = true;
                        }
                    }
                }
            }
        }
        bool is_raw_or_smart_ptr = (vd.type && (vd.type->is_ptr || vd.type->is_mut_ptr || !vd.type->smart_wrap.empty())) ||
                                   is_ptr_or_smart_init(vd.init);
        bool is_ptr_or_smart = is_raw_or_smart_ptr || is_opt_var;
        bool is_scalar_prim = false;
        if (vd.type) {
            is_scalar_prim = is_scalar_primitive_typeref(*vd.type);
        } else if (!is_opt_var && !is_fallible_var && !init_was_unhandled_fallible && !is_raw_or_smart_ptr && !is_byte_slice && vd.init) {
            if (const auto* lit = std::get_if<LiteralExpr>(&vd.init->node)) {
                is_scalar_prim = (lit->kind == TokenKind::Int || lit->kind == TokenKind::Float ||
                                  lit->kind == TokenKind::KwTrue || lit->kind == TokenKind::KwFalse);
            } else if (std::holds_alternative<BinaryExpr>(vd.init->node) || std::holds_alternative<UnaryExpr>(vd.init->node)) {
                is_scalar_prim = is_scalar_primitive_type_name(inferred_t);
            } else {
                const Expr* cur_init = vd.init;
                if (const auto* tr = std::get_if<TryExpr>(&cur_init->node)) cur_init = tr->inner;
                else if (const auto* or_ex = std::get_if<OrExpr>(&cur_init->node)) cur_init = or_ex->inner;
                if (cur_init) {
                    if (const auto* call_ex = std::get_if<CallExpr>(&cur_init->node)) {
                        if (const auto* cid = std::get_if<IdentExpr>(&call_ex->callee->node)) {
                            if (auto fit = cf_.fn_sigs.find(cid->name); fit != cf_.fn_sigs.end()) {
                                TypeRef rt = fit->second.return_type;
                                rt.is_fallible = false;
                                rt.is_optional = false;
                                is_scalar_prim = is_scalar_primitive_typeref(rt);
                            } else if (is_scalar_primitive_type_name(cid->name)) {
                                is_scalar_prim = true;
                            }
                        } else if (const auto* mem = std::get_if<MemberExpr>(&call_ex->callee->node)) {
                            if (const auto* recv_id = std::get_if<IdentExpr>(&mem->object->node)) {
                                if (const auto* std_sig = StdlibCatalogIndex::instance().find_ns_fn(recv_id->name, mem->field)) {
                                    is_scalar_prim = is_scalar_primitive_type_name(std_sig->unwrapped_return_type);
                                }
                            }
                        }
                    }
                }
            }
        }
        auto init_ns = infer_expr_null_state(vd.init);

        bind_scoped_var(vd.name, vd.is_mut);
        map_vars_.erase(vd.name);
        fixed_array_lens_.erase(vd.name);
        if (is_byte_slice) byte_slice_vars_.insert(vd.name);
        else byte_slice_vars_.erase(vd.name);
        if (is_smart) smart_ptr_vars_.insert(vd.name);
        else smart_ptr_vars_.erase(vd.name);
        if (is_fallible_var) fallible_vars_.insert(vd.name);
        else fallible_vars_.erase(vd.name);
        if (init_was_unhandled_fallible) reported_unhandled_vars_.insert(vd.name);
        else reported_unhandled_vars_.erase(vd.name);
        if (is_raw_or_smart_ptr) raw_or_smart_ptr_vars_.insert(vd.name);
        else raw_or_smart_ptr_vars_.erase(vd.name);
        if (is_scalar_prim) scalar_primitive_vars_.insert(vd.name);
        else scalar_primitive_vars_.erase(vd.name);
        if (vd.type && vd.type->is_fn && vd.type->fn_ret) {
            if (vd.type->fn_ret->is_fallible) fn_ret_fallible_vars_.insert(vd.name);
            else fn_ret_fallible_vars_.erase(vd.name);
            if (vd.type->fn_ret->is_optional) fn_ret_optional_vars_.insert(vd.name);
            else fn_ret_optional_vars_.erase(vd.name);
        } else {
            fn_ret_fallible_vars_.erase(vd.name);
            fn_ret_optional_vars_.erase(vd.name);
        }

        if (vd.type) {
            if (vd.type->is_map) {
                map_vars_.insert(vd.name);
            }
            if (vd.type->is_array && !vd.type->array_len.empty()) {
                if (auto n = cfire::detail::parse_int<std::size_t>(vd.type->array_len)) {
                    fixed_array_lens_[vd.name] = *n;
                }
            }
        } else if (!vd.is_mut && vd.init) {
            if (const auto* arr_lit = std::get_if<ArrayLitExpr>(&vd.init->node)) {
                fixed_array_lens_[vd.name] = arr_lit->elements.size();
            }
        }
        if (vd.init) {
            if (const auto* call_ex = std::get_if<CallExpr>(&vd.init->node)) {
                if (const auto* cid = std::get_if<IdentExpr>(&call_ex->callee->node);
                    cid && cid->name == "make" && call_ex->type_args.size() == 1 && call_ex->type_args[0].is_map) {
                    map_vars_.insert(vd.name);
                }
            }
        }

        if (src) {
            borrow_origin_[vd.name] = *src;
        }
        if (!inferred_t.empty()) {
            var_types_[vd.name] = std::move(inferred_t);
        } else {
            var_types_.erase(vd.name);
        }
        if (is_opt_var) {
            optional_vars_.insert(vd.name);
        } else {
            optional_vars_.erase(vd.name);
        }
        if (is_ptr_or_smart) {
            ptr_or_smart_vars_.insert(vd.name);
        } else {
            ptr_or_smart_vars_.erase(vd.name);
        }
        if (init_ns) {
            ptr_null_state_[vd.name] = *init_ns;
        } else if (!vd.init && is_ptr_or_smart) {
            ptr_null_state_[vd.name] = NullState::Null;
        } else if (is_opt_var) {
            if (vd.init && (std::holds_alternative<StructLitExpr>(vd.init->node) ||
                            (std::holds_alternative<LiteralExpr>(vd.init->node) &&
                             std::get<LiteralExpr>(vd.init->node).kind != TokenKind::KwNone))) {
                ptr_null_state_[vd.name] = NullState::NonNull;
            } else {
                ptr_null_state_[vd.name] = NullState::MaybeNull;
            }
        } else if (vd.type && (vd.type->is_ptr || vd.type->is_mut_ptr || !vd.type->smart_wrap.empty())) {
            ptr_null_state_[vd.name] = vd.type->is_optional ? NullState::MaybeNull : NullState::NonNull;
        } else {
            ptr_null_state_.erase(vd.name);
        }
        return {};
    }

    [[nodiscard]] auto check_destructure_var_decl_stmt(const DestructureVarDeclStmt& dvs) -> std::expected<void, std::string> {
        if (auto r = check_expr(dvs.init); !r) return r;
        std::string elem_type;
        if (dvs.init) {
            const Expr* inner = dvs.init;
            if (const auto* tr = std::get_if<TryExpr>(&inner->node)) inner = tr->inner;
            else if (const auto* orex = std::get_if<OrExpr>(&inner->node)) inner = orex->inner;
            if (inner) {
                if (const auto* call = std::get_if<CallExpr>(&inner->node)) {
                    if (const auto* mem = std::get_if<MemberExpr>(&call->callee->node)) {
                        if (const auto* obj_id = std::get_if<IdentExpr>(&mem->object->node)) {
                            if (obj_id->name == "os" && mem->field == "pipe") elem_type = "File";
                            else if (obj_id->name == "net" && mem->field == "socketpair") elem_type = "TcpConn";
                            else if (obj_id->name == "websocket" && mem->field == "pair") elem_type = "websocket.Conn";
                        }
                    }
                }
            }
        }
        for (const auto& [name, is_mut] : dvs.bindings) {
            bind_scoped_var(name, is_mut);
            if (!elem_type.empty()) {
                var_types_[name] = elem_type;
            }
        }
        return {};
    }

    [[nodiscard]] auto check_assign_stmt(const AssignStmt& as) -> std::expected<void, std::string> {
        if (as.value && as.target) {
            if (const auto* id = std::get_if<IdentExpr>(&as.target->node)) {
                if (id->name == "_" || fallible_vars_.contains(id->name)) {
                    unwrapped_exprs_.insert(as.value);
                }
            }
        }
        if (auto r = check_expr(as.value); !r) return r;
        if ((as.op == "/=" || as.op == "%=") && is_const_zero_expr(as.value)) {
            if (auto r = emit_error(std::format(
                "{}:{}:1: division or modulo assignment by constant zero (cfire/DIVIDE-BY-ZERO)",
                path_, as.line)); !r) return r;
        }
        if (as.op != "=") {
            if (auto r = check_expr(as.target); !r) return r;
        }
        if (const auto* id = std::get_if<IdentExpr>(&as.target->node)) {
            if (byte_slice_vars_.contains(id->name) && as.value) {
                if (const auto* arr_lit = std::get_if<ArrayLitExpr>(&as.value->node)) {
                    for (const auto& el : arr_lit->elements) {
                        if (is_non_byte_int_or_u8_expr(el)) {
                            if (auto r = emit_error(std::format(
                                "{}:{}:{}: cannot use integer/u8 value in '[]byte' literal without explicit 'byte(...)' conversion (cfire/TYPE-MISMATCH)",
                                path_, el->line, el->col)); !r) return r;
                        }
                    }
                }
            }
            if (imm_vars_.contains(id->name)) {
                if (auto r = emit_error(std::format(
                    "{}:{}:{}: cannot assign to immutable variable '{}' (cfire/ASSIGN-IMMUTABLE)",
                    path_, as.line, 1, id->name)); !r) return r;
            }
            if (auto r = check_not_iterated_collection(id->name, as.line, 1); !r) return r;
            moved_at_line_.erase(id->name);
            if (as.op == "=") {
                invalidate_loans_for_owner(
                    id->name,
                    std::format("underlying owner '{}' was reassigned at line {}", id->name, as.line),
                    as.line);
                if (auto src = extract_borrow_source(as.value)) {
                    auto d_target = var_scope_depth_.find(id->name);
                    auto d_src = var_scope_depth_.find(*src);
                    if (d_target != var_scope_depth_.end() && d_src != var_scope_depth_.end()) {
                        if (d_src->second > d_target->second) {
                            if (auto r = emit_error(std::format(
                                "{}:{}:1: pointer/view '{}' cannot borrow '{}' from an inner scope (cfire/DANGLING-POINTER)",
                                path_, as.line, id->name, *src)); !r) return r;
                        }
                    }
                    borrow_origin_[id->name] = *src;
                    invalidated_loans_.erase(id->name);
                } else {
                    borrow_origin_.erase(id->name);
                    invalidated_loans_.erase(id->name);
                }
                if (is_ptr_or_smart_init(as.value)) {
                    ptr_or_smart_vars_.insert(id->name);
                }
                if (auto ns = infer_expr_null_state(as.value)) {
                    ptr_null_state_[id->name] = *ns;
                } else if (ptr_null_state_.contains(id->name)) {
                    ptr_null_state_[id->name] = NullState::NonNull;
                }
            }
        } else {
            if (const auto* idx = std::get_if<IndexExpr>(&as.target->node)) {
                if (is_byte_slice_expr(idx->object) && is_non_byte_int_or_u8_expr(as.value)) {
                    if (auto r = emit_error(std::format(
                        "{}:{}:1: cannot assign integer/u8 value to '[]byte' element without explicit 'byte(...)' conversion (cfire/TYPE-MISMATCH)",
                        path_, as.line)); !r) return r;
                }
            }
            if (as.op == "=") {
                if (auto r = check_expr(as.target); !r) return r;
            }
        }
        return {};
    }

    [[nodiscard]] auto check_destructure_assign_stmt(const DestructureAssignStmt& das) -> std::expected<void, std::string> {
        if (auto r = check_expr(das.value); !r) return r;
        for (const auto& targ : das.targets) {
            if (const auto* id = std::get_if<IdentExpr>(&targ->node)) {
                if (imm_vars_.contains(id->name)) {
                    if (auto r = emit_error(std::format(
                        "{}:{}:{}: cannot assign to immutable variable '{}' (cfire/ASSIGN-IMMUTABLE)",
                        path_, das.line, 1, id->name)); !r) return r;
                }
                moved_at_line_.erase(id->name);
            } else {
                if (auto r = check_expr(targ); !r) return r;
            }
        }
        return {};
    }

    [[nodiscard]] auto check_defer_stmt(const DeferStmt& df) -> std::expected<void, std::string> {
        if (df.call) {
            unwrapped_exprs_.insert(df.call);
            return check_expr(df.call);
        }
        ++in_defer_depth_;
        int saved_loop_depth = loop_depth_;
        loop_depth_ = 0;
        auto defer_guard = cfire::scope_exit([this, saved_loop_depth] {
            --in_defer_depth_;
            loop_depth_ = saved_loop_depth;
        });
        ScopeGuard sg(*this);
        return check_block_stmts(df.body);
    }

    [[nodiscard]] auto check_return_stmt(const ReturnStmt& rt) -> std::expected<void, std::string> {
        if (in_defer_depth_ > 0) {
            if (auto r = emit_error(std::format(
                "{}:{}:1: cannot use 'return' inside a 'defer {{ ... }}' block (cfire/RETURN-IN-DEFER)",
                path_, rt.line)); !r) return r;
        }
        if (rt.value && (fn_is_fallible_ || in_closure_depth_ > 0)) {
            unwrapped_exprs_.insert(rt.value);
        }
        if (auto r = check_expr(rt.value); !r) return r;
        if (rt.value) {
            bool is_direct = false;
            if (auto src = extract_borrow_source(rt.value, &is_direct)) {
                if (local_defined_vars_.contains(*src) || move_params_.contains(*src)) {
                    if (auto r = emit_error(std::format(
                        "{}:{}:1: cannot return pointer/view borrowing local variable '{}' (cfire/DANGLING-RETURN)",
                        path_, rt.line, *src)); !r) return r;
                }
            }
        }
        return {};
    }

    [[nodiscard]] auto check_if_stmt(const IfStmt& ifs) -> std::expected<void, std::string> {
        if (auto r = check_expr(ifs.cond); !r) return r;
        auto initial_moves = moved_at_line_;
        auto initial_loans = invalidated_loans_;
        auto initial_null = ptr_null_state_;
        auto nc = extract_null_check(ifs.cond);

        if (nc) {
            ptr_null_state_[nc->first] = nc->second ? NullState::NonNull : NullState::Null;
        }
        {
            ScopeGuard sg(*this);
            if (auto r = check_block_stmts(ifs.then_body); !r) return r;
        }
        auto then_moves = moved_at_line_;
        auto then_loans = invalidated_loans_;
        auto then_null = ptr_null_state_;

        moved_at_line_ = initial_moves;
        invalidated_loans_ = initial_loans;
        ptr_null_state_ = initial_null;
        if (nc) {
            ptr_null_state_[nc->first] = nc->second ? NullState::Null : NullState::NonNull;
        }
        {
            ScopeGuard sg(*this);
            if (auto r = check_block_stmts(ifs.else_body); !r) return r;
        }
        auto else_moves = moved_at_line_;
        auto else_loans = invalidated_loans_;
        auto else_null = ptr_null_state_;

        bool then_div = block_diverges(ifs.then_body);
        bool else_div = !ifs.else_body.empty() && block_diverges(ifs.else_body);

        if (ifs.else_body.empty()) {
            moved_at_line_ = initial_moves;
            if (then_div) {
                invalidated_loans_ = else_loans;
                ptr_null_state_ = else_null;
            } else {
                invalidated_loans_ = then_loans;
                for (const auto& [k, v] : initial_loans) {
                    invalidated_loans_.try_emplace(k, v);
                }
                std::unordered_map<std::string, NullState> merged_null = initial_null;
                for (const auto& [var, st] : then_null) {
                    if (nc && var == nc->first) {
                        continue;
                    }
                    auto it = initial_null.find(var);
                    if (it == initial_null.end() || it->second == st) {
                        merged_null[var] = st;
                    } else {
                        merged_null[var] = NullState::MaybeNull;
                    }
                }
                ptr_null_state_ = std::move(merged_null);
            }
        } else {
            if (then_div) {
                moved_at_line_ = else_moves;
                invalidated_loans_ = else_loans;
                ptr_null_state_ = else_null;
            } else if (else_div) {
                moved_at_line_ = then_moves;
                invalidated_loans_ = then_loans;
                ptr_null_state_ = then_null;
            } else {
                std::unordered_map<std::string, int> post_moves = initial_moves;
                for (const auto& [var, line] : then_moves) {
                    if (else_moves.contains(var)) {
                        post_moves[var] = std::max(line, else_moves[var]);
                    }
                }
                moved_at_line_ = std::move(post_moves);

                std::unordered_map<std::string, InvalidatedLoan> post_loans = then_loans;
                for (const auto& [var, loan] : else_loans) {
                    post_loans.try_emplace(var, loan);
                }
                invalidated_loans_ = std::move(post_loans);

                std::unordered_map<std::string, NullState> merged_null = initial_null;
                for (const auto& [var, st_then] : then_null) {
                    auto it_else = else_null.find(var);
                    if (it_else != else_null.end() && it_else->second == st_then) {
                        merged_null[var] = st_then;
                    } else {
                        merged_null[var] = NullState::MaybeNull;
                    }
                }
                ptr_null_state_ = std::move(merged_null);
            }
        }
        return {};
    }

    [[nodiscard]] auto check_for_stmt(const ForStmt& fs) -> std::expected<void, std::string> {
        if (fs.item_var.empty()) {
            ++in_invalid_unwrap_context_;
            auto guard = cfire::scope_exit([this] { --in_invalid_unwrap_context_; });
            if (auto r = check_expr(fs.iter_expr); !r) return r;
        } else {
            if (auto r = check_expr(fs.iter_expr); !r) return r;
            if (fs.range_end) {
                if (auto r = check_expr(fs.range_end); !r) return r;
            }
        }
        auto pre_loop_moves = moved_at_line_;
        auto pre_loop_loans = invalidated_loans_;
        std::string locked_col;
        if (!fs.item_var.empty() && !fs.range_end) {
            if (auto pi = extract_path_info(fs.iter_expr); pi && pi->path == pi->root) {
                if (!active_loop_collections_.contains(pi->root)) {
                    locked_col = pi->root;
                    active_loop_collections_.insert(locked_col);
                }
            }
        }
        auto unlock_guard = cfire::scope_exit([this, &locked_col] {
            if (!locked_col.empty()) active_loop_collections_.erase(locked_col);
        });
        {
            ++loop_depth_;
            auto loop_guard = cfire::scope_exit([this] { --loop_depth_; });
            ScopeGuard sg(*this);
            if (!fs.index_var.empty()) {
                bind_scoped_var(fs.index_var, false);
            }
            if (!fs.item_var.empty()) {
                bind_scoped_var(fs.item_var, false);
            }
            if (auto r = check_block_stmts(fs.body); !r) return r;
        }

        for (const auto& [var, line] : moved_at_line_) {
            if (!pre_loop_moves.contains(var)) {
                if (auto r = emit_error(std::format(
                    "{}:{}:{}: variable '{}' moved inside loop body without reassignment before next iteration (cfire/USE-AFTER-MOVE)",
                    path_, line, 1, var)); !r) return r;
            }
        }
        for (const auto& [var, loan] : invalidated_loans_) {
            if (!pre_loop_loans.contains(var) && var_scope_depth_.contains(var) && var_scope_depth_[var] <= scope_depth_) {
                // Loan on outer pointer invalidated inside loop; remains invalidated after loop
            }
        }
        return {};
    }

    [[nodiscard]] auto check_async_scope_stmt(const AsyncScopeStmt& sc) -> std::expected<void, std::string> {
        if (auto r = check_expr(sc.workers); !r) return r;
        ScopeGuard sg(*this);
        if (!sc.scope_var.empty()) {
            bind_scoped_var(sc.scope_var, false);
        }
        return check_block_stmts(sc.body);
    }

private:
    std::string_view path_;
    std::string pkg_name_{};
    std::string fn_name_{};
    std::string fn_ret_str_{};
    const CheckedFile& cf_;
    std::vector<std::string>& warnings_;
    std::vector<std::string>& errors_;
    bool resilient_{false};
    std::vector<std::string> fallback_errors_{};
    bool fn_is_fallible_{false};
    bool has_cpp_imports_{false};
    bool seen_cpp_block_{false};
    int scope_depth_{1};
    int in_defer_depth_{0};
    int in_closure_depth_{0};
    int loop_depth_{0};
    int in_invalid_unwrap_context_{0};
    std::vector<std::unordered_map<std::string, SavedVarDeclState>*> scope_stack_{};
    std::unordered_set<const Expr*> unwrapped_exprs_{};
    std::unordered_set<const Expr*> discarded_or_exprs_{};
    std::unordered_map<std::string, int> var_scope_depth_{};
    std::unordered_set<std::string> local_defined_vars_{};
    std::unordered_map<std::string, std::string> borrow_origin_{};
    std::unordered_map<std::string, InvalidatedLoan> invalidated_loans_{};
    std::unordered_set<std::string> active_loop_collections_{};
    std::unordered_set<std::string> ptr_or_smart_vars_{};
    std::unordered_set<std::string> raw_or_smart_ptr_vars_{};
    std::unordered_set<std::string> smart_ptr_vars_{};
    std::unordered_set<std::string> byte_slice_vars_{};
    std::unordered_set<std::string> optional_vars_{};
    std::unordered_set<std::string> fallible_vars_{};
    std::unordered_set<std::string> fn_ret_fallible_vars_{};
    std::unordered_set<std::string> fn_ret_optional_vars_{};
    std::unordered_set<std::string> reported_unhandled_vars_{};
    std::unordered_set<std::string> scalar_primitive_vars_{};
    std::unordered_set<std::string> reported_missing_imports_{};
    std::unordered_map<std::string, std::string> var_types_{};
    std::unordered_map<std::string, NullState> ptr_null_state_{};
    std::unordered_map<std::string, std::size_t> fixed_array_lens_{};
    std::unordered_set<std::string> map_vars_{};
    std::unordered_map<std::string, int> moved_at_line_{};
    std::unordered_set<std::string> move_params_{};
    std::unordered_set<std::string> mut_vars_{};
    std::unordered_set<std::string> imm_vars_{};
};


[[nodiscard]] inline auto analyze_resilient(FileAST ast) -> CheckedFile {
    CheckedFile cf;
    cf.ast = std::move(ast);

    // 0a. Check Module Imports (CF-006 / CF-086)
    for (const auto& imp : cf.ast.imports) {
        if (!imp.is_cpp) {
            if (auto canon = cfire::modules::canonical_import_for_wrong_prefix(imp.path); !canon.empty()) {
                if (canon == "ext:crypto/sodium") {
                    cf.errors.push_back(std::format(
                        "{}:{}:1: module '{}' requires native libsodium and must be imported as 'ext:crypto/sodium' (cfire/INVALID-IMPORT)",
                        cf.ast.path, imp.line, imp.path));
                } else if (canon.starts_with("ext:")) {
                    cf.errors.push_back(std::format(
                        "{}:{}:1: module '{}' requires a native extension engine and must be imported as '{}' (cfire/INVALID-IMPORT)",
                        cf.ast.path, imp.line, imp.path, canon));
                } else {
                    cf.errors.push_back(std::format(
                        "{}:{}:1: module '{}' is part of the zero-setup core standard library and must be imported as '{}' (cfire/INVALID-IMPORT)",
                        cf.ast.path, imp.line, imp.path, canon));
                }
            }
        }
    }

    // 0. Check Global / Package Variables
    for (const auto& gv : cf.ast.global_vars) {
        if (gv.was_var) {
            std::string_view gv_path = gv.source_path.empty() ? std::string_view(cf.ast.path) : std::string_view(gv.source_path);
            cf.warnings.push_back(std::format(
                "{}:{}:1: warning: 'var' is deprecated; use 'x := val' or 'mut x: T = val' instead (cfire/DEPRECATED-VAR)",
                gv_path, gv.line));
        }
    }

    // 1. Index Enums and their Variants
    for (const auto& en : cf.ast.enums) {
        cf.enums[en.name] = en;
        for (const auto& v : en.variants) {
            cf.variant_to_enum[v.name] = en.name;
        }
    }

    // 2. Index Struct Field Tags & Validate Tag Invariants (TEST-03a)
    for (const auto& st : cf.ast.structs) {
        std::string_view st_path = st.source_path.empty() ? std::string_view(cf.ast.path) : std::string_view(st.source_path);
        std::vector<StructTagInfo> tags;
        std::unordered_map<std::string, std::string> seen_json_keys;
        std::unordered_map<std::string, std::string> seen_db_cols;
        std::unordered_map<std::string, std::string> seen_cli_longs;
        std::unordered_map<std::string, std::string> seen_cli_shorts;

        for (const auto& fld : st.fields) {
            StructTagInfo info;
            info.field_name = fld.name;
            info.field_type = fld.type;
            if (fld.raw_tag.empty()) {
                tags.push_back(std::move(info));
                continue;
            }

            for (const auto& [key, val] : parse_struct_tags(fld.raw_tag)) {
                if (key == "cli") {
                    auto comma = val.find(',');
                    if (comma != std::string::npos) {
                        info.cli_long = trim_str(std::string_view(val).substr(0, comma));
                        info.cli_short = trim_str(std::string_view(val).substr(comma + 1));
                    } else {
                        info.cli_long = trim_str(val);
                    }
                    if (!info.cli_long.empty() && info.cli_long != "-") {
                        if (auto prev = seen_cli_longs.find(info.cli_long); prev != seen_cli_longs.end()) {
                            cf.errors.push_back(std::format(
                                "{}:{}:1: duplicate cli flag '--{}' on field '{}.{}' (already used by field '{}') (cfire/INVALID-STRUCT-TAG)",
                                st_path, fld.line, info.cli_long, st.name, fld.name, prev->second));
                        } else {
                            seen_cli_longs[info.cli_long] = fld.name;
                        }
                    }
                    if (!info.cli_short.empty() && info.cli_short != "-") {
                        if (auto prev = seen_cli_shorts.find(info.cli_short); prev != seen_cli_shorts.end()) {
                            cf.errors.push_back(std::format(
                                "{}:{}:1: duplicate cli short flag '-{}' on field '{}.{}' (already used by field '{}') (cfire/INVALID-STRUCT-TAG)",
                                st_path, fld.line, info.cli_short, st.name, fld.name, prev->second));
                        } else {
                            seen_cli_shorts[info.cli_short] = fld.name;
                        }
                    }
                } else if (key == "help") {
                    info.cli_help = val;
                } else if (key == "json") {
                    auto comma = val.find(',');
                    info.json_key = trim_str(comma != std::string_view::npos ? std::string_view(val).substr(0, comma) : std::string_view(val));
                    if (!info.json_key.empty() && info.json_key != "-") {
                        if (auto prev = seen_json_keys.find(info.json_key); prev != seen_json_keys.end()) {
                            cf.errors.push_back(std::format(
                                "{}:{}:1: duplicate json tag key '{}' on field '{}.{}' (already used by field '{}') (cfire/INVALID-STRUCT-TAG)",
                                st_path, fld.line, info.json_key, st.name, fld.name, prev->second));
                        } else {
                            seen_json_keys[info.json_key] = fld.name;
                        }
                    }
                } else if (key == "db" || key == "sql") {
                    info.db_column = trim_str(val);
                    if (!info.db_column.empty() && info.db_column != "-") {
                        if (auto prev = seen_db_cols.find(info.db_column); prev != seen_db_cols.end()) {
                            cf.errors.push_back(std::format(
                                "{}:{}:1: duplicate {} column tag '{}' on field '{}.{}' (already used by field '{}') (cfire/INVALID-STRUCT-TAG)",
                                st_path, fld.line, key, info.db_column, st.name, fld.name, prev->second));
                        } else {
                            seen_db_cols[info.db_column] = fld.name;
                        }
                    }
                } else if (key == "default") {
                    std::string dval = trim_str(val);
                    const std::string& tname = fld.type.name;
                    if (tname == "i8" || tname == "i16" || tname == "i32" || tname == "i64" || tname == "int") {
                        if (!cfire::detail::parse_int<std::int64_t>(dval)) {
                            cf.errors.push_back(std::format(
                                "{}:{}:1: invalid default tag value '{}' for integer field '{}.{}' of type '{}' (cfire/INVALID-STRUCT-TAG)",
                                st_path, fld.line, dval, st.name, fld.name, tname));
                        }
                    } else if (tname == "u8" || tname == "u16" || tname == "u32" || tname == "u64" || tname == "usize" || tname == "byte") {
                        if (dval.starts_with('-') || !cfire::detail::parse_int<std::uint64_t>(dval)) {
                            cf.errors.push_back(std::format(
                                "{}:{}:1: invalid default tag value '{}' for unsigned field '{}.{}' of type '{}' (cfire/INVALID-STRUCT-TAG)",
                                st_path, fld.line, dval, st.name, fld.name, tname));
                        }
                    } else if (tname == "bool") {
                        if (dval != "true" && dval != "false" && dval != "1" && dval != "0") {
                            cf.errors.push_back(std::format(
                                "{}:{}:1: invalid default tag value '{}' for bool field '{}.{}' (expected 'true' or 'false') (cfire/INVALID-STRUCT-TAG)",
                                st_path, fld.line, dval, st.name, fld.name));
                        }
                    } else if (tname == "f32" || tname == "f64") {
                        char* endp = nullptr;
                        (void)std::strtod(dval.c_str(), &endp);
                        if (dval.empty() || endp != dval.c_str() + dval.size()) {
                            cf.errors.push_back(std::format(
                                "{}:{}:1: invalid default tag value '{}' for float field '{}.{}' of type '{}' (cfire/INVALID-STRUCT-TAG)",
                                st_path, fld.line, dval, st.name, fld.name, tname));
                        }
                    }
                } else if (key == "validate") {
                    const std::string& tname = fld.type.name;
                    bool is_int_type = (tname == "i8" || tname == "i16" || tname == "i32" || tname == "i64" || tname == "isize" || tname == "int");
                    bool is_uint_type = (tname == "u8" || tname == "u16" || tname == "u32" || tname == "u64" || tname == "usize" || tname == "byte");
                    bool is_float_type = (tname == "f32" || tname == "f64");
                    bool is_num_type = is_int_type || is_uint_type || is_float_type;
                    bool is_len_type = (tname == "str" || fld.type.is_slice);

                    std::size_t rpos = 0;
                    while (rpos <= val.size()) {
                        auto comma = val.find(',', rpos);
                        std::string r_tok = trim_str(comma == std::string::npos
                            ? std::string_view(val).substr(rpos)
                            : std::string_view(val).substr(rpos, comma - rpos));
                        rpos = (comma == std::string::npos) ? (val.size() + 1) : (comma + 1);
                        if (r_tok.empty()) continue;

                        if (r_tok == "non_empty") {
                            if (!is_len_type) {
                                cf.errors.push_back(std::format(
                                    "{}:{}:1: validate rule 'non_empty' on field '{}.{}' requires 'str' or slice type, got '{}' (cfire/INVALID-STRUCT-TAG)",
                                    st_path, fld.line, st.name, fld.name, tname));
                            } else {
                                info.validation.non_empty = true;
                                info.validation.has_rules = true;
                            }
                        } else if (r_tok.starts_with("min=") || r_tok.starts_with("max=")) {
                            bool is_min = r_tok.starts_with("min=");
                            std::string nstr = trim_str(std::string_view(r_tok).substr(4));
                            if (!is_num_type) {
                                cf.errors.push_back(std::format(
                                    "{}:{}:1: validate rule '{}' on field '{}.{}' requires a numeric field type, got '{}' (cfire/INVALID-STRUCT-TAG)",
                                    st_path, fld.line, r_tok, st.name, fld.name, tname));
                            } else {
                                char* endp = nullptr;
                                double num = std::strtod(nstr.c_str(), &endp);
                                bool valid_num = !nstr.empty() && endp == nstr.c_str() + nstr.size();
                                if (is_int_type && !cfire::detail::parse_int<std::int64_t>(nstr)) valid_num = false;
                                if (is_uint_type && (nstr.starts_with('-') || !cfire::detail::parse_int<std::uint64_t>(nstr))) valid_num = false;
                                if (!valid_num) {
                                    cf.errors.push_back(std::format(
                                        "{}:{}:1: invalid numeric bound '{}' in validate tag on field '{}.{}' of type '{}' (cfire/INVALID-STRUCT-TAG)",
                                        st_path, fld.line, r_tok, st.name, fld.name, tname));
                                } else {
                                    info.validation.has_rules = true;
                                    if (is_min) {
                                        info.validation.has_min = true;
                                        info.validation.min_raw = nstr;
                                        info.validation.min_num = num;
                                    } else {
                                        info.validation.has_max = true;
                                        info.validation.max_raw = nstr;
                                        info.validation.max_num = num;
                                    }
                                }
                            }
                        } else if (r_tok.starts_with("min_len=") || r_tok.starts_with("max_len=")) {
                            bool is_min_l = r_tok.starts_with("min_len=");
                            std::string lstr = trim_str(std::string_view(r_tok).substr(8));
                            if (!is_len_type) {
                                cf.errors.push_back(std::format(
                                    "{}:{}:1: validate rule '{}' on field '{}.{}' requires 'str' or slice type, got '{}' (cfire/INVALID-STRUCT-TAG)",
                                    st_path, fld.line, r_tok, st.name, fld.name, tname));
                            } else {
                                auto parsed_len = (!lstr.starts_with('-')) ? cfire::detail::parse_int<std::uint64_t>(lstr) : std::nullopt;
                                if (!parsed_len) {
                                    cf.errors.push_back(std::format(
                                        "{}:{}:1: invalid length bound '{}' in validate tag on field '{}.{}' (cfire/INVALID-STRUCT-TAG)",
                                        st_path, fld.line, r_tok, st.name, fld.name));
                                } else {
                                    info.validation.has_rules = true;
                                    if (is_min_l) {
                                        info.validation.has_min_len = true;
                                        info.validation.min_len = static_cast<std::size_t>(*parsed_len);
                                    } else {
                                        info.validation.has_max_len = true;
                                        info.validation.max_len = static_cast<std::size_t>(*parsed_len);
                                    }
                                }
                            }
                        } else if (r_tok.starts_with("oneof=")) {
                            std::string opts_str = trim_str(std::string_view(r_tok).substr(6));
                            if (tname != "str") {
                                cf.errors.push_back(std::format(
                                    "{}:{}:1: validate rule '{}' on field '{}.{}' requires 'str' field type, got '{}' (cfire/INVALID-STRUCT-TAG)",
                                    st_path, fld.line, r_tok, st.name, fld.name, tname));
                            } else {
                                std::vector<std::string> opts;
                                std::size_t opos = 0;
                                while (opos <= opts_str.size()) {
                                    auto bar = opts_str.find('|', opos);
                                    std::string item = trim_str(bar == std::string::npos
                                        ? std::string_view(opts_str).substr(opos)
                                        : std::string_view(opts_str).substr(opos, bar - opos));
                                    opos = (bar == std::string::npos) ? (opts_str.size() + 1) : (bar + 1);
                                    if (!item.empty()) opts.push_back(std::move(item));
                                }
                                if (opts.empty()) {
                                    cf.errors.push_back(std::format(
                                        "{}:{}:1: empty 'oneof=' option list in validate tag on field '{}.{}' (cfire/INVALID-STRUCT-TAG)",
                                        st_path, fld.line, st.name, fld.name));
                                } else {
                                    info.validation.has_rules = true;
                                    info.validation.oneof = std::move(opts);
                                }
                            }
                        } else {
                            cf.errors.push_back(std::format(
                                "{}:{}:1: unknown validate tag rule '{}' on field '{}.{}' (expected min=, max=, min_len=, max_len=, non_empty, or oneof=) (cfire/INVALID-STRUCT-TAG)",
                                st_path, fld.line, r_tok, st.name, fld.name));
                        }
                    }
                    if (info.validation.has_min && info.validation.has_max && info.validation.min_num > info.validation.max_num) {
                        cf.errors.push_back(std::format(
                            "{}:{}:1: validate tag on field '{}.{}' has min ({}) > max ({}) (cfire/INVALID-STRUCT-TAG)",
                            st_path, fld.line, st.name, fld.name, info.validation.min_raw, info.validation.max_raw));
                    }
                    if (info.validation.has_min_len && info.validation.has_max_len && info.validation.min_len > info.validation.max_len) {
                        cf.errors.push_back(std::format(
                            "{}:{}:1: validate tag on field '{}.{}' has min_len ({}) > max_len ({}) (cfire/INVALID-STRUCT-TAG)",
                            st_path, fld.line, st.name, fld.name, info.validation.min_len, info.validation.max_len));
                    }
                }
                info.custom_tags[key] = val;
            }
            tags.push_back(std::move(info));
        }
        cf.struct_tags[st.name] = std::move(tags);
    }

    // 3. Index Functions & Methods
    for (const auto& fn : cf.ast.functions) {
        if (fn.receiver) {
            std::string key = fn.receiver->type.name + "::" + fn.name;
            cf.methods[key] = fn;
            cf.method_owner[fn.name] = fn.receiver->type.name;
            cf.method_owners[fn.name].insert(fn.receiver->type.name);
        } else {
            if (!fn.package_name.empty()) {
                cf.fn_sigs[fn.package_name + "." + fn.name] = fn;
            }
            if (fn.package_name.empty() || !cf.fn_sigs.contains(fn.name)) {
                cf.fn_sigs[fn.name] = fn;
            }
        }
    }

    // 4. Validate Function & Method Bodies
    for (const auto& fn : cf.ast.functions) {
        std::string_view fn_path = fn.source_path.empty() ? std::string_view(cf.ast.path) : std::string_view(fn.source_path);
        SemaFnChecker checker(fn_path, fn, cf, cf.warnings, cf.errors, true);
        for (const auto& st : fn.body) {
            (void)checker.check_stmt(st);
        }
        std::string fn_key = fn.receiver ? (fn.receiver->type.name + "__" + fn.name) : fn.name;
        for (const auto& [p_name, ln] : checker.last_read_line) {
            cf.last_use_move[fn_key + ":" + p_name] = ln;
            if (!fn.package_name.empty() && !fn.receiver) {
                cf.last_use_move[std::format("cfire_pkg_{}__{}:{}", fn.package_name, fn.name, p_name)] = ln;
            }
        }
    }

    // 5. Validate Test, Fuzz, and Bench Declarations
    for (std::size_t idx = 0; idx < cf.ast.tests.size(); ++idx) {
        const auto& td = cf.ast.tests[idx];
        std::string_view td_path = td.source_path.empty() ? std::string_view(cf.ast.path) : std::string_view(td.source_path);
        FnDecl dummy_fn;
        dummy_fn.name = std::format("__cfire_test_{}", idx);
        dummy_fn.return_type.is_fallible = true;
        dummy_fn.params = td.params;
        for (const auto& bv : td.bench_variations) {
            dummy_fn.params.push_back(ParamDecl{"in", bv.name, bv.inferred_type, bv.line, 1});
        }
        SemaFnChecker checker(td_path, dummy_fn, cf, cf.warnings, cf.errors, true);
        for (const auto& bv : td.bench_variations) {
            for (const auto& vex : bv.values) {
                (void)checker.check_expr(vex);
            }
        }
        for (const auto& st : td.body) {
            (void)checker.check_stmt(st);
        }
    }

    return cf;
}

[[nodiscard]] inline auto analyze(FileAST ast) -> std::expected<CheckedFile, std::string> {
    auto cf = analyze_resilient(std::move(ast));
    if (!cf.errors.empty()) {
        return std::unexpected(cf.errors.front());
    }
    return cf;
}

} // namespace cfire::frontend
