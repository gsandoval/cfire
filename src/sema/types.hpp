#pragma once
// src/sema/types.hpp — C++23 Semantic Types, Interned TypeTable, and CheckedFile

#include "../ast.hpp"
#include "../diagnostics.hpp"
#include "../module_registry.hpp"
#include "../parser.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
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

enum class TypeKind : uint8_t {
    Unknown = 0,
    Never,
    Void,
    Bool,
    Int8, Int16, Int32, Int64,
    Uint8, Uint16, Uint32, Uint64,
    Isize, Usize,
    Float32, Float64,
    Str,
    Byte,
    Error,
    Foreign,
    NominalStruct,
    NominalEnum,
    Interface,
    TypeParam,
    GenericInst,
    Slice,
    Array,
    Map,
    Optional,
    Fallible,
    Own,
    Shared,
    Borrow,
    MutBorrow,
    RawPtr,
    MutPtr,
    Tuple,
    Function
};

struct TypeId {
    uint32_t index{0};

    [[nodiscard]] constexpr bool is_unknown() const noexcept { return index == 0; }
    [[nodiscard]] constexpr bool is_valid() const noexcept { return index != 0; }
    [[nodiscard]] constexpr bool is_never() const noexcept { return index == 1; }
    [[nodiscard]] constexpr bool is_void() const noexcept { return index == 2; }
    [[nodiscard]] constexpr bool is_error() const noexcept { return index == 18; }
    [[nodiscard]] constexpr bool is_foreign() const noexcept { return index == 19; }

    constexpr bool operator==(const TypeId& o) const noexcept = default;
    constexpr auto operator<=>(const TypeId& o) const noexcept = default;

    static constexpr TypeId Unknown() noexcept { return TypeId{0}; }
    static constexpr TypeId Never() noexcept { return TypeId{1}; }
    static constexpr TypeId Void() noexcept { return TypeId{2}; }
    static constexpr TypeId Bool() noexcept { return TypeId{3}; }
    static constexpr TypeId Int8() noexcept { return TypeId{4}; }
    static constexpr TypeId Int16() noexcept { return TypeId{5}; }
    static constexpr TypeId Int32() noexcept { return TypeId{6}; }
    static constexpr TypeId Int64() noexcept { return TypeId{7}; }
    static constexpr TypeId Uint8() noexcept { return TypeId{8}; }
    static constexpr TypeId Uint16() noexcept { return TypeId{9}; }
    static constexpr TypeId Uint32() noexcept { return TypeId{10}; }
    static constexpr TypeId Uint64() noexcept { return TypeId{11}; }
    static constexpr TypeId Isize() noexcept { return TypeId{12}; }
    static constexpr TypeId Usize() noexcept { return TypeId{13}; }
    static constexpr TypeId Float32() noexcept { return TypeId{14}; }
    static constexpr TypeId Float64() noexcept { return TypeId{15}; }
    static constexpr TypeId Str() noexcept { return TypeId{16}; }
    // `byte` is an alias of `u8` (slot 17 is kept so the builtin ids stay stable).
    static constexpr TypeId Byte() noexcept { return TypeId{8}; }
    static constexpr TypeId Error() noexcept { return TypeId{18}; }
    static constexpr TypeId Foreign() noexcept { return TypeId{19}; }
};

struct TypeDesc {
    TypeKind kind{TypeKind::Unknown};
    std::string name{};
    std::string base_name{};
    std::string package_name{};
    TypeId elem_type{TypeId::Unknown()};
    TypeId key_type{TypeId::Unknown()};
    std::vector<TypeId> param_types{};
    std::vector<TypeId> type_args{};
    std::vector<std::string> constraints{};
    TypeId ret_type{TypeId::Unknown()};
    std::size_t array_len{0};
    std::size_t fn_min_params{static_cast<std::size_t>(-1)};
};

class TypeTable {
public:
    TypeTable() {
        types_.reserve(64);
        types_.push_back(TypeDesc{TypeKind::Unknown, "unknown"});
        types_.push_back(TypeDesc{TypeKind::Never, "never"});
        types_.push_back(TypeDesc{TypeKind::Void, "void"});
        types_.push_back(TypeDesc{TypeKind::Bool, "bool"});
        types_.push_back(TypeDesc{TypeKind::Int8, "i8"});
        types_.push_back(TypeDesc{TypeKind::Int16, "i16"});
        types_.push_back(TypeDesc{TypeKind::Int32, "i32"});
        types_.push_back(TypeDesc{TypeKind::Int64, "i64"});
        types_.push_back(TypeDesc{TypeKind::Uint8, "u8"});
        types_.push_back(TypeDesc{TypeKind::Uint16, "u16"});
        types_.push_back(TypeDesc{TypeKind::Uint32, "u32"});
        types_.push_back(TypeDesc{TypeKind::Uint64, "u64"});
        types_.push_back(TypeDesc{TypeKind::Isize, "isize"});
        types_.push_back(TypeDesc{TypeKind::Usize, "usize"});
        types_.push_back(TypeDesc{TypeKind::Float32, "f32"});
        types_.push_back(TypeDesc{TypeKind::Float64, "f64"});
        types_.push_back(TypeDesc{TypeKind::Str, "str"});
        types_.push_back(TypeDesc{TypeKind::Byte, "byte"});
        types_.push_back(TypeDesc{TypeKind::Error, "<error>"});
        types_.push_back(TypeDesc{TypeKind::Foreign, "<foreign>"});

        for (uint32_t i = 0; i < types_.size(); ++i) {
            name_to_id_[types_[i].name] = TypeId{i};
        }
        name_to_id_["byte"] = TypeId::Uint8();
        name_to_id_["int"] = TypeId::Int64();
        name_to_id_["uint"] = TypeId::Uint64();
        name_to_id_["bool_"] = TypeId::Bool();
    }

    [[nodiscard]] auto get(TypeId id) const -> const TypeDesc& {
        if (id.index >= types_.size()) return types_[0];
        return types_[id.index];
    }

    [[nodiscard]] auto to_string(TypeId id) const -> std::string {
        return get(id).name;
    }

    [[nodiscard]] auto intern(TypeDesc desc) const -> TypeId {
        if (!desc.name.empty()) {
            if (auto it = name_to_id_.find(desc.name); it != name_to_id_.end()) {
                return it->second;
            }
        }
        uint32_t next_id = static_cast<uint32_t>(types_.size());
        if (desc.name.empty()) {
            desc.name = format_desc(desc);
        }
        name_to_id_[desc.name] = TypeId{next_id};
        types_.push_back(std::move(desc));
        return TypeId{next_id};
    }

    [[nodiscard]] auto from_name(std::string_view name, const std::unordered_map<std::string, TypeId>* type_params = nullptr) const -> TypeId {
        if (name.empty()) return TypeId::Unknown();
        std::string n(name);
        if (type_params != nullptr) {
            if (auto it = type_params->find(n); it != type_params->end()) {
                return it->second;
            }
        }
        if (auto it = name_to_id_.find(n); it != name_to_id_.end()) {
            return it->second;
        }
        if (n.starts_with("fn(")) {
            int p_depth = 0;
            int b_depth = 0;
            int c_depth = 0;
            bool in_q = false;
            std::size_t close_p = std::string::npos;
            std::vector<TypeId> params;
            std::size_t min_params = static_cast<std::size_t>(-1);
            auto parse_fn_param = [&](std::string_view tok, std::size_t idx) -> TypeId {
                tok = trim_view(tok);
                // Strip top-level default value (`= ...`) while tracking optionality (`min_params`).
                int dp = 0, db = 0, dc = 0;
                bool dq = false;
                for (std::size_t k = 0; k < tok.size(); ++k) {
                    char ch = tok[k];
                    if (ch == '"' && (k == 0 || tok[k - 1] != '\\')) dq = !dq;
                    else if (!dq) {
                        if (ch == '(') ++dp;
                        else if (ch == ')') --dp;
                        else if (ch == '[') ++db;
                        else if (ch == ']') --db;
                        else if (ch == '{') ++dc;
                        else if (ch == '}') --dc;
                        else if (ch == '=' && dp == 0 && db == 0 && dc == 0) {
                            min_params = std::min(min_params, idx);
                            tok = trim_view(tok.substr(0, k));
                            break;
                        }
                    }
                }
                if (tok.starts_with("mut ")) tok = trim_view(tok.substr(4));
                else if (tok.starts_with("move ")) tok = trim_view(tok.substr(5));
                else if (tok.starts_with("out ")) tok = trim_view(tok.substr(4));
                else if (tok.starts_with("in ")) tok = trim_view(tok.substr(3));
                // Strip top-level parameter name prefix (`name: Type`).
                dp = 0; db = 0; dc = 0; dq = false;
                for (std::size_t k = 0; k < tok.size(); ++k) {
                    char ch = tok[k];
                    if (ch == '"' && (k == 0 || tok[k - 1] != '\\')) dq = !dq;
                    else if (!dq) {
                        if (ch == '(') ++dp;
                        else if (ch == ')') --dp;
                        else if (ch == '[') ++db;
                        else if (ch == ']') --db;
                        else if (ch == '{') ++dc;
                        else if (ch == '}') --dc;
                        else if (ch == ':' && dp == 0 && db == 0 && dc == 0) {
                            if (k + 1 < tok.size() && tok[k + 1] == ':') {
                                ++k;
                                continue;
                            }
                            tok = trim_view(tok.substr(k + 1));
                            break;
                        }
                    }
                }
                if (tok.starts_with("mut ")) tok = trim_view(tok.substr(4));
                else if (tok.starts_with("move ")) tok = trim_view(tok.substr(5));
                else if (tok.starts_with("out ")) tok = trim_view(tok.substr(4));
                else if (tok.starts_with("in ")) tok = trim_view(tok.substr(3));
                return from_name(tok, type_params);
            };
            std::size_t start = 3;
            for (std::size_t i = 2; i < n.size(); ++i) {
                char c = n[i];
                if (c == '"' && (i == 0 || n[i - 1] != '\\')) {
                    in_q = !in_q;
                } else if (!in_q) {
                    if (c == '(') ++p_depth;
                    else if (c == ')') {
                        --p_depth;
                        if (p_depth == 0) {
                            close_p = i;
                            break;
                        }
                    } else if (c == '[') ++b_depth;
                    else if (c == ']') --b_depth;
                    else if (c == '{') ++c_depth;
                    else if (c == '}') --c_depth;
                    else if (c == ',' && p_depth == 1 && b_depth == 0 && c_depth == 0) {
                        params.push_back(parse_fn_param(std::string_view(n.data() + start, i - start), params.size()));
                        start = i + 1;
                    }
                }
            }
            if (close_p != std::string::npos) {
                std::string_view last_part = trim_view(std::string_view(n.data() + start, close_p - start));
                if (!last_part.empty() || !params.empty()) {
                    if (!last_part.empty()) params.push_back(parse_fn_param(last_part, params.size()));
                }
                std::size_t eff_min = (min_params < params.size()) ? min_params : params.size();
                std::string_view rest = trim_view(std::string_view(n.data() + close_p + 1, n.size() - (close_p + 1)));
                if (rest.empty()) {
                    return make_function(std::move(params), TypeId::Void(), eff_min);
                }
                if (rest == "?") {
                    return make_optional(make_function(std::move(params), TypeId::Void(), eff_min));
                }
                if (rest == "!") {
                    return make_fallible(make_function(std::move(params), TypeId::Void(), eff_min));
                }
                if (rest.starts_with("->")) {
                    rest = trim_view(rest.substr(2));
                    TypeId ret = from_name(rest, type_params);
                    return make_function(std::move(params), ret, eff_min);
                }
            }
        }
        if (n.ends_with('?')) {
            TypeId inner = from_name(n.substr(0, n.size() - 1), type_params);
            return make_optional(inner);
        }
        if (n.ends_with('!')) {
            TypeId inner = from_name(n.substr(0, n.size() - 1), type_params);
            return make_fallible(inner);
        }
        if (n.starts_with("[]")) {
            TypeId elem = from_name(n.substr(2), type_params);
            return make_slice(elem);
        }
        // `map[K]V` (K may itself contain brackets) and `[N]T`.
        if (n.starts_with("map[")) {
            int depth = 0;
            for (std::size_t i = 3; i < n.size(); ++i) {
                if (n[i] == '[') ++depth;
                else if (n[i] == ']' && --depth == 0) {
                    TypeId key = from_name(n.substr(4, i - 4), type_params);
                    TypeId val = from_name(n.substr(i + 1), type_params);
                    if (key.is_unknown() || val.is_unknown()) return TypeId::Unknown();
                    return make_map(key, val);
                }
            }
        }
        if (n.size() > 2 && n[0] == '[' && std::isdigit(static_cast<unsigned char>(n[1]))) {
            if (auto rb = n.find(']'); rb != std::string::npos) {
                if (auto parsed_len = parse_int_literal<std::size_t>(n.substr(1, rb - 1))) {
                    TypeId elem = from_name(n.substr(rb + 1), type_params);
                    if (!elem.is_unknown()) return make_array(elem, *parsed_len);
                } else {
                    return TypeId::Unknown();
                }
            }
        }
        if (n.size() >= 2 && n.front() == '(' && n.back() == ')') {
            int p_depth = 0;
            int b_depth = 0;
            bool outer_encloses = true;
            std::vector<TypeId> elems;
            std::size_t start = 1;
            for (std::size_t i = 0; i < n.size(); ++i) {
                if (n[i] == '(') ++p_depth;
                else if (n[i] == ')') {
                    --p_depth;
                    if (p_depth == 0 && i + 1 < n.size()) {
                        outer_encloses = false;
                        break;
                    }
                } else if (n[i] == '[') ++b_depth;
                else if (n[i] == ']') --b_depth;
                else if (n[i] == ',' && p_depth == 1 && b_depth == 0) {
                    elems.push_back(from_name(trim_view(std::string_view(n.data() + start, i - start)), type_params));
                    start = i + 1;
                }
            }
            if (outer_encloses && p_depth == 0) {
                std::string_view last_part = trim_view(std::string_view(n.data() + start, (n.size() - 1) - start));
                if (!last_part.empty() || !elems.empty()) {
                    elems.push_back(from_name(last_part, type_params));
                    return make_tuple(std::move(elems));
                }
            }
        }
        if (n.starts_with("&mut ")) {
            TypeId inner = from_name(n.substr(5), type_params);
            return make_mut_borrow(inner);
        }
        if (n.starts_with('&')) {
            TypeId inner = from_name(n.substr(1), type_params);
            return make_borrow(inner);
        }
        if (n.starts_with("*mut ")) {
            TypeId inner = from_name(n.substr(5), type_params);
            return make_mut_ptr(inner);
        }
        if (n.starts_with('*')) {
            TypeId inner = from_name(n.substr(1), type_params);
            return make_raw_ptr(inner);
        }
        if (n.starts_with("own[") && n.ends_with(']')) {
            TypeId inner = from_name(n.substr(4, n.size() - 5), type_params);
            return make_own(inner);
        }
        if (n.starts_with("shared[") && n.ends_with(']')) {
            TypeId inner = from_name(n.substr(7, n.size() - 8), type_params);
            return make_shared(inner);
        }
        if (auto lb = n.find('['); lb != std::string::npos && lb > 0 && n.back() == ']') {
            int depth = 0;
            bool outer_encloses = true;
            std::vector<TypeId> args;
            std::size_t start = lb + 1;
            int p_depth = 0;
            for (std::size_t i = lb; i < n.size(); ++i) {
                if (n[i] == '[') ++depth;
                else if (n[i] == ']') {
                    --depth;
                    if (depth == 0 && i + 1 < n.size()) {
                        outer_encloses = false;
                        break;
                    }
                } else if (n[i] == '(') ++p_depth;
                else if (n[i] == ')') --p_depth;
                else if (n[i] == ',' && depth == 1 && p_depth == 0) {
                    args.push_back(from_name(trim_view(std::string_view(n.data() + start, i - start)), type_params));
                    start = i + 1;
                }
            }
            if (outer_encloses && depth == 0) {
                std::string_view last_arg = trim_view(std::string_view(n.data() + start, (n.size() - 1) - start));
                if (!last_arg.empty() || !args.empty()) {
                    args.push_back(from_name(last_arg, type_params));
                    return make_generic_inst(std::string_view(n.data(), lb), std::move(args));
                }
            }
        }
        TypeDesc d;
        d.kind = TypeKind::NominalStruct;
        d.name = n;
        return intern(std::move(d));
    }

    [[nodiscard]] auto from_typeref(const TypeRef& tr, const std::unordered_map<std::string, TypeId>* type_params = nullptr) const -> TypeId {
        if (tr.is_fallible) {
            TypeRef inner = tr;
            inner.is_fallible = false;
            return make_fallible(from_typeref(inner, type_params));
        }
        if (tr.is_optional) {
            TypeRef inner = tr;
            inner.is_optional = false;
            return make_optional(from_typeref(inner, type_params));
        }
        if (tr.is_fn) {
            std::vector<TypeId> params;
            params.reserve(tr.fn_params.size());
            for (const auto& pt : tr.fn_params) params.push_back(from_typeref(pt, type_params));
            TypeId ret = tr.fn_ret ? from_typeref(*tr.fn_ret, type_params) : TypeId::Void();
            return make_function(std::move(params), ret);
        }
        if (tr.is_tuple) {
            std::vector<TypeId> elems;
            elems.reserve(tr.tuple_types.size());
            for (const auto& tt : tr.tuple_types) elems.push_back(from_typeref(tt, type_params));
            return make_tuple(std::move(elems));
        }
        if (tr.is_slice) {
            TypeId elem = tr.elem_type
                ? from_typeref(*tr.elem_type, type_params)
                : from_name(tr.name.starts_with("[]") ? std::string_view(tr.name).substr(2) : std::string_view(tr.name), type_params);
            return make_slice(elem);
        }
        if (tr.is_array) {
            TypeId elem = tr.elem_type ? from_typeref(*tr.elem_type, type_params) : from_name(tr.name, type_params);
            std::size_t len = 0;
            if (!tr.array_len.empty()) {
                len = parse_int_literal<std::size_t>(tr.array_len).value_or(0);
            }
            return make_array(elem, len);
        }
        if (tr.is_map) {
            TypeId k = tr.key_type ? from_typeref(*tr.key_type, type_params) : TypeId::Str();
            TypeId v = tr.elem_type ? from_typeref(*tr.elem_type, type_params) : TypeId::Unknown();
            return make_map(k, v);
        }
        if (!tr.smart_wrap.empty()) {
            TypeId elem = tr.elem_type ? from_typeref(*tr.elem_type, type_params) : from_name(tr.name, type_params);
            if (tr.smart_wrap == "own") return make_own(elem);
            if (tr.smart_wrap == "shared") return make_shared(elem);
        }
        if (tr.is_borrow) {
            TypeId elem = tr.elem_type ? from_typeref(*tr.elem_type, type_params) : from_name(tr.name, type_params);
            return make_borrow(elem);
        }
        if (tr.is_mut_borrow) {
            TypeId elem = tr.elem_type ? from_typeref(*tr.elem_type, type_params) : from_name(tr.name, type_params);
            return make_mut_borrow(elem);
        }
        if (tr.is_ptr) {
            TypeId elem = tr.elem_type ? from_typeref(*tr.elem_type, type_params) : from_name(tr.name, type_params);
            return make_raw_ptr(elem);
        }
        if (tr.is_mut_ptr) {
            TypeId elem = tr.elem_type ? from_typeref(*tr.elem_type, type_params) : from_name(tr.name, type_params);
            return make_mut_ptr(elem);
        }
        if (!tr.type_args.empty()) {
            std::vector<TypeId> args;
            args.reserve(tr.type_args.size());
            for (const auto& ta : tr.type_args) {
                args.push_back(from_typeref(ta, type_params));
            }
            return make_generic_inst(tr.name, std::move(args));
        }
        return from_name(tr.name, type_params);
    }

    [[nodiscard]] auto make_type_param(std::string_view name, std::vector<std::string> constraints = {}) const -> TypeId {
        std::vector<std::string> sorted_constraints = constraints;
        std::sort(sorted_constraints.begin(), sorted_constraints.end());
        std::string key = std::string(name) + "|";
        for (std::size_t i = 0; i < sorted_constraints.size(); ++i) {
            if (i > 0) key += ',';
            key += sorted_constraints[i];
        }
        if (auto it = type_param_cache_.find(key); it != type_param_cache_.end()) {
            return it->second;
        }
        TypeDesc d;
        d.kind = TypeKind::TypeParam;
        d.name = std::string(name);
        d.constraints = std::move(constraints);
        uint32_t next_id = static_cast<uint32_t>(types_.size());
        type_param_cache_[key] = TypeId{next_id};
        types_.push_back(std::move(d));
        return TypeId{next_id};
    }

    [[nodiscard]] auto make_generic_inst(std::string_view base, std::vector<TypeId> args) const -> TypeId {
        if (args.size() == 1 && base == "own") return make_own(args[0]);
        if (args.size() == 1 && base == "shared") return make_shared(args[0]);
        TypeDesc d;
        d.kind = TypeKind::GenericInst;
        d.base_name = std::string(base);
        d.type_args = std::move(args);
        return intern_composite(std::move(d));
    }

    [[nodiscard]] auto make_function(std::vector<TypeId> params, TypeId ret,
                                     std::size_t min_params = static_cast<std::size_t>(-1)) const -> TypeId {
        TypeDesc d;
        d.kind = TypeKind::Function;
        d.fn_min_params = (min_params <= params.size()) ? min_params : params.size();
        d.param_types = std::move(params);
        d.ret_type = ret;
        return intern_composite(std::move(d));
    }

    [[nodiscard]] auto make_slice(TypeId elem) const -> TypeId {
        TypeDesc d;
        d.kind = TypeKind::Slice;
        d.elem_type = elem;
        return intern_composite(std::move(d));
    }

    [[nodiscard]] auto make_array(TypeId elem, std::size_t len) const -> TypeId {
        TypeDesc d;
        d.kind = TypeKind::Array;
        d.elem_type = elem;
        d.array_len = len;
        return intern_composite(std::move(d));
    }

    [[nodiscard]] auto make_map(TypeId key, TypeId val) const -> TypeId {
        TypeDesc d;
        d.kind = TypeKind::Map;
        d.key_type = key;
        d.elem_type = val;
        return intern_composite(std::move(d));
    }

    [[nodiscard]] auto make_optional(TypeId base) const -> TypeId {
        if (base.is_unknown()) return TypeId::Unknown();
        if (base.is_error()) return TypeId::Error();
        TypeDesc d;
        d.kind = TypeKind::Optional;
        d.elem_type = base;
        return intern_composite(std::move(d));
    }

    [[nodiscard]] auto make_fallible(TypeId base) const -> TypeId {
        if (base.is_unknown()) return TypeId::Unknown();
        if (base.is_error()) return TypeId::Error();
        TypeDesc d;
        d.kind = TypeKind::Fallible;
        d.elem_type = base;
        return intern_composite(std::move(d));
    }

    [[nodiscard]] auto make_own(TypeId base) const -> TypeId {
        TypeDesc d;
        d.kind = TypeKind::Own;
        d.elem_type = base;
        return intern_composite(std::move(d));
    }

    [[nodiscard]] auto make_shared(TypeId base) const -> TypeId {
        TypeDesc d;
        d.kind = TypeKind::Shared;
        d.elem_type = base;
        return intern_composite(std::move(d));
    }

    [[nodiscard]] auto make_borrow(TypeId base) const -> TypeId {
        TypeDesc d;
        d.kind = TypeKind::Borrow;
        d.elem_type = base;
        return intern_composite(std::move(d));
    }

    [[nodiscard]] auto make_mut_borrow(TypeId base) const -> TypeId {
        TypeDesc d;
        d.kind = TypeKind::MutBorrow;
        d.elem_type = base;
        return intern_composite(std::move(d));
    }

    [[nodiscard]] auto make_raw_ptr(TypeId base) const -> TypeId {
        TypeDesc d;
        d.kind = TypeKind::RawPtr;
        d.elem_type = base;
        return intern_composite(std::move(d));
    }

    [[nodiscard]] auto make_mut_ptr(TypeId base) const -> TypeId {
        TypeDesc d;
        d.kind = TypeKind::MutPtr;
        d.elem_type = base;
        return intern_composite(std::move(d));
    }

    [[nodiscard]] auto make_tuple(std::vector<TypeId> elems) const -> TypeId {
        TypeDesc d;
        d.kind = TypeKind::Tuple;
        d.param_types = std::move(elems);
        return intern_composite(std::move(d));
    }

    [[nodiscard]] auto unwrap_optional(TypeId id) const -> TypeId {
        const auto& d = get(id);
        if (d.kind == TypeKind::Optional) return d.elem_type;
        return id;
    }

    [[nodiscard]] auto unwrap_fallible(TypeId id) const -> TypeId {
        const auto& d = get(id);
        if (d.kind == TypeKind::Fallible) return d.elem_type;
        return id;
    }

    [[nodiscard]] auto is_error(TypeId id) const -> bool {
        return get(id).kind == TypeKind::Error;
    }

    [[nodiscard]] auto is_foreign(TypeId id) const -> bool {
        return get(id).kind == TypeKind::Foreign;
    }

    [[nodiscard]] auto is_type_param(TypeId id) const -> bool {
        return get(id).kind == TypeKind::TypeParam;
    }

    [[nodiscard]] auto is_optional(TypeId id) const -> bool {
        return get(id).kind == TypeKind::Optional;
    }

    [[nodiscard]] auto is_fallible(TypeId id) const -> bool {
        return get(id).kind == TypeKind::Fallible;
    }

    [[nodiscard]] auto is_never_or_error(TypeId id) const -> bool {
        auto k = get(id).kind;
        return k == TypeKind::Never || k == TypeKind::Error;
    }

    [[nodiscard]] auto contains_unknown(TypeId id) const -> bool {
        return any_type_part(id, [](TypeId t) { return t.is_unknown(); });
    }

    [[nodiscard]] auto contains_error(TypeId id) const -> bool {
        return any_type_part(id, [](TypeId t) { return t.is_error(); });
    }

    [[nodiscard]] auto contains_foreign(TypeId id) const -> bool {
        return any_type_part(id, [](TypeId t) { return t.is_foreign(); });
    }

    [[nodiscard]] auto contains_type_param(TypeId id) const -> bool {
        return any_type_part(id, [this](TypeId t) { return get(t).kind == TypeKind::TypeParam; });
    }

    [[nodiscard]] auto has_constraint(TypeId id, std::string_view constraint) const -> bool {
        const auto& d = get(id);
        if (d.kind != TypeKind::TypeParam) return false;
        for (const auto& bound : d.constraints) {
            if (bound == constraint) return true;
            if (constraint == "Comparable" &&
                (bound == "Ordered" || bound == "Numeric" || bound == "Integer" || bound == "Equatable")) {
                return true;
            }
            if (constraint == "Ordered" && (bound == "Numeric" || bound == "Integer")) {
                return true;
            }
            if (constraint == "Numeric" && bound == "Integer") {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto is_numeric(TypeId id) const -> bool {
        const auto& d = get(id);
        return d.kind == TypeKind::Int8 || d.kind == TypeKind::Int16 || d.kind == TypeKind::Int32 ||
               d.kind == TypeKind::Int64 || d.kind == TypeKind::Uint8 || d.kind == TypeKind::Uint16 ||
               d.kind == TypeKind::Uint32 || d.kind == TypeKind::Uint64 || d.kind == TypeKind::Isize ||
               d.kind == TypeKind::Usize || d.kind == TypeKind::Float32 || d.kind == TypeKind::Float64 ||
               d.kind == TypeKind::Byte;
    }

    [[nodiscard]] auto is_integer(TypeId id) const -> bool {
        const auto& d = get(id);
        return d.kind == TypeKind::Int8 || d.kind == TypeKind::Int16 || d.kind == TypeKind::Int32 ||
               d.kind == TypeKind::Int64 || d.kind == TypeKind::Uint8 || d.kind == TypeKind::Uint16 ||
               d.kind == TypeKind::Uint32 || d.kind == TypeKind::Uint64 || d.kind == TypeKind::Isize ||
               d.kind == TypeKind::Usize || d.kind == TypeKind::Byte;
    }

    [[nodiscard]] auto is_integer_or_constrained(TypeId id) const -> bool {
        return is_integer(id) || has_constraint(id, "Integer");
    }

    [[nodiscard]] auto is_numeric_or_constrained(TypeId id) const -> bool {
        return is_numeric(id) || has_constraint(id, "Numeric") || has_constraint(id, "Integer");
    }

    [[nodiscard]] auto is_float(TypeId id) const -> bool {
        const auto& d = get(id);
        return d.kind == TypeKind::Float32 || d.kind == TypeKind::Float64;
    }

    [[nodiscard]] auto substitute(TypeId t, const std::unordered_map<std::string, TypeId>& env) const -> TypeId {
        if (env.empty() || t.is_unknown() || t.index <= 19) return t;
        const auto& d = get(t);
        switch (d.kind) {
            case TypeKind::TypeParam: {
                if (auto it = env.find(d.name); it != env.end()) return it->second;
                return t;
            }
            case TypeKind::Slice: {
                TypeId ne = substitute(d.elem_type, env);
                return ne == d.elem_type ? t : make_slice(ne);
            }
            case TypeKind::Array: {
                TypeId ne = substitute(d.elem_type, env);
                return ne == d.elem_type ? t : make_array(ne, d.array_len);
            }
            case TypeKind::Map: {
                TypeId nk = substitute(d.key_type, env);
                TypeId nv = substitute(d.elem_type, env);
                return (nk == d.key_type && nv == d.elem_type) ? t : make_map(nk, nv);
            }
            case TypeKind::Optional: {
                TypeId ne = substitute(d.elem_type, env);
                return ne == d.elem_type ? t : make_optional(ne);
            }
            case TypeKind::Fallible: {
                TypeId ne = substitute(d.elem_type, env);
                return ne == d.elem_type ? t : make_fallible(ne);
            }
            case TypeKind::Own: {
                TypeId ne = substitute(d.elem_type, env);
                return ne == d.elem_type ? t : make_own(ne);
            }
            case TypeKind::Shared: {
                TypeId ne = substitute(d.elem_type, env);
                return ne == d.elem_type ? t : make_shared(ne);
            }
            case TypeKind::Borrow: {
                TypeId ne = substitute(d.elem_type, env);
                return ne == d.elem_type ? t : make_borrow(ne);
            }
            case TypeKind::MutBorrow: {
                TypeId ne = substitute(d.elem_type, env);
                return ne == d.elem_type ? t : make_mut_borrow(ne);
            }
            case TypeKind::RawPtr: {
                TypeId ne = substitute(d.elem_type, env);
                return ne == d.elem_type ? t : make_raw_ptr(ne);
            }
            case TypeKind::MutPtr: {
                TypeId ne = substitute(d.elem_type, env);
                return ne == d.elem_type ? t : make_mut_ptr(ne);
            }
            case TypeKind::Tuple: {
                bool changed = false;
                std::vector<TypeId> elems;
                elems.reserve(d.param_types.size());
                for (TypeId p : d.param_types) {
                    TypeId np = substitute(p, env);
                    changed = changed || (np != p);
                    elems.push_back(np);
                }
                return changed ? make_tuple(std::move(elems)) : t;
            }
            case TypeKind::Function: {
                bool changed = false;
                std::vector<TypeId> params;
                params.reserve(d.param_types.size());
                for (TypeId p : d.param_types) {
                    TypeId np = substitute(p, env);
                    changed = changed || (np != p);
                    params.push_back(np);
                }
                TypeId nr = substitute(d.ret_type, env);
                changed = changed || (nr != d.ret_type);
                return changed ? make_function(std::move(params), nr, d.fn_min_params) : t;
            }
            case TypeKind::GenericInst: {
                bool changed = false;
                std::vector<TypeId> args;
                args.reserve(d.type_args.size());
                for (TypeId a : d.type_args) {
                    TypeId na = substitute(a, env);
                    changed = changed || (na != a);
                    args.push_back(na);
                }
                return changed ? make_generic_inst(d.base_name, std::move(args)) : t;
            }
            default:
                return t;
        }
    }

    [[nodiscard]] auto unify(TypeId pattern, TypeId concrete, std::unordered_map<std::string, TypeId>& bindings) const -> bool {
        if (concrete.is_error() || concrete.is_never() || concrete.is_foreign()) return true;
        if (pattern.is_unknown() || concrete.is_unknown()) return false;
        const auto& pd = get(pattern);
        const auto& cd = get(concrete);
        if (pd.kind == TypeKind::TypeParam) {
            if (auto it = bindings.find(pd.name); it != bindings.end()) {
                return it->second == concrete;
            }
            bindings[pd.name] = concrete;
            return true;
        }
        if (pattern == concrete) return true;
        if (pd.kind == TypeKind::Optional) {
            if (cd.kind == TypeKind::Optional) return unify(pd.elem_type, cd.elem_type, bindings);
            return unify(pd.elem_type, concrete, bindings);
        }
        if (pd.kind == TypeKind::Fallible) {
            if (cd.kind == TypeKind::Fallible) return unify(pd.elem_type, cd.elem_type, bindings);
            if (cd.name == "Error" || cd.name == "cfire::Error") return true;
            return unify(pd.elem_type, concrete, bindings);
        }
        if (pd.kind == TypeKind::Slice && cd.kind == TypeKind::Array) {
            return unify(pd.elem_type, cd.elem_type, bindings);
        }
        if (pd.kind == TypeKind::Borrow) {
            if (cd.kind == TypeKind::Borrow || cd.kind == TypeKind::MutBorrow) {
                return unify(pd.elem_type, cd.elem_type, bindings);
            }
            return unify(pd.elem_type, concrete, bindings);
        }
        if (pattern == TypeId::Str() && cd.kind == TypeKind::Borrow && cd.elem_type == TypeId::Str()) {
            return true;
        }
        if (pd.kind != cd.kind) {
            if (!contains_type_param(pattern)) return is_assignable(pattern, concrete);
            return false;
        }
        switch (pd.kind) {
            case TypeKind::Slice:
            case TypeKind::Own:
            case TypeKind::Shared:
            case TypeKind::MutBorrow:
            case TypeKind::RawPtr:
            case TypeKind::MutPtr:
                return unify(pd.elem_type, cd.elem_type, bindings);
            case TypeKind::Array:
                return pd.array_len == cd.array_len && unify(pd.elem_type, cd.elem_type, bindings);
            case TypeKind::Map:
                return unify(pd.key_type, cd.key_type, bindings) && unify(pd.elem_type, cd.elem_type, bindings);
            case TypeKind::Tuple:
                if (pd.param_types.size() != cd.param_types.size()) return false;
                for (std::size_t i = 0; i < pd.param_types.size(); ++i) {
                    if (!unify(pd.param_types[i], cd.param_types[i], bindings)) return false;
                }
                return true;
            case TypeKind::Function:
                if (pd.param_types.size() != cd.param_types.size()) return false;
                for (std::size_t i = 0; i < pd.param_types.size(); ++i) {
                    if (!unify(pd.param_types[i], cd.param_types[i], bindings)) return false;
                }
                return unify(pd.ret_type, cd.ret_type, bindings);
            case TypeKind::GenericInst:
                if (pd.base_name != cd.base_name || pd.type_args.size() != cd.type_args.size()) return false;
                for (std::size_t i = 0; i < pd.type_args.size(); ++i) {
                    if (!unify(pd.type_args[i], cd.type_args[i], bindings)) return false;
                }
                return true;
            default:
                return false;
        }
    }

    // (bits, signed) of an integer type; {0, false} for anything else. `isize`/`usize` are 64-bit.
    [[nodiscard]] auto int_shape(TypeId id) const -> std::pair<int, bool> {
        switch (get(id).kind) {
            case TypeKind::Int8: return {8, true};
            case TypeKind::Int16: return {16, true};
            case TypeKind::Int32: return {32, true};
            case TypeKind::Int64: case TypeKind::Isize: return {64, true};
            case TypeKind::Uint8: case TypeKind::Byte: return {8, false};
            case TypeKind::Uint16: return {16, false};
            case TypeKind::Uint32: return {32, false};
            case TypeKind::Uint64: case TypeKind::Usize: return {64, false};
            default: return {0, false};
        }
    }

    // every value of integer type `src` fits in `dest` (only these convert implicitly).
    [[nodiscard]] auto int_widens(TypeId dest, TypeId src) const -> bool {
        const auto [db, ds] = int_shape(dest);
        const auto [sb, ss] = int_shape(src);
        if (db == 0 || sb == 0) return false;
        if (ds == ss) return db >= sb;
        return ds && db > sb;  // unsigned into a strictly wider signed type
    }

    [[nodiscard]] auto is_assignable(TypeId dest, TypeId src) const -> bool {
        if (dest == src) return true;
        if (src.is_never()) return true;
        if (dest.is_error() || src.is_error()) return true;
        if (dest.is_foreign() || src.is_foreign()) return true;
        if (dest.is_unknown() || src.is_unknown()) return false;
        if (contains_error(dest) || contains_error(src)) return true;
        const auto& dest_d = get(dest);
        const auto& src_d = get(src);
        if (dest_d.kind == TypeKind::Optional && (dest_d.elem_type == src || is_assignable(dest_d.elem_type, src))) return true;
        if (dest_d.kind == TypeKind::Fallible && (dest_d.elem_type == src || is_assignable(dest_d.elem_type, src) || src_d.name == "Error" || src_d.name == "cfire::Error")) return true;
        if (is_integer(dest) && is_integer(src)) return true;
        if (is_float(dest) && is_numeric(src)) return true;
        if (dest_d.kind == TypeKind::Borrow && dest_d.elem_type == TypeId::Str() && src == TypeId::Str()) return true;
        // a `&str` view is copied implicitly wherever an owned `str` is required.
        if (dest == TypeId::Str() && src_d.kind == TypeKind::Borrow && src_d.elem_type == TypeId::Str()) return true;
        if (dest_d.kind == TypeKind::Borrow && dest_d.elem_type == src) return true;
        if (dest_d.kind == TypeKind::Slice && src_d.kind == TypeKind::Array &&
            (dest_d.elem_type == src_d.elem_type || dest_d.elem_type.is_foreign() || src_d.elem_type.is_foreign())) return true;
        if (dest_d.kind == TypeKind::Tuple && src_d.kind == TypeKind::Tuple &&
            dest_d.param_types.size() == src_d.param_types.size()) {
            for (std::size_t i = 0; i < dest_d.param_types.size(); ++i) {
                if (!is_assignable(dest_d.param_types[i], src_d.param_types[i])) return false;
            }
            return true;
        }
        if (dest_d.kind == TypeKind::GenericInst && src_d.kind == TypeKind::GenericInst &&
            dest_d.base_name == src_d.base_name && dest_d.type_args.size() == src_d.type_args.size()) {
            for (std::size_t i = 0; i < dest_d.type_args.size(); ++i) {
                TypeId da = dest_d.type_args[i];
                TypeId sa = src_d.type_args[i];
                if (da != sa && !da.is_foreign() && !sa.is_foreign() && !da.is_error() && !sa.is_error()) {
                    return false;
                }
            }
            return true;
        }
        if (dest_d.kind == TypeKind::Function && src_d.kind == TypeKind::Function &&
            dest_d.param_types.size() == src_d.param_types.size()) {
            for (std::size_t i = 0; i < dest_d.param_types.size(); ++i) {
                TypeId dp = dest_d.param_types[i];
                TypeId sp = src_d.param_types[i];
                if (dp != sp && !dp.is_foreign() && !sp.is_foreign() && !dp.is_error() && !sp.is_error()) {
                    return false;
                }
            }
            TypeId dr = dest_d.ret_type;
            TypeId sr = src_d.ret_type;
            if (dr == sr || dr.is_foreign() || sr.is_foreign() || dr.is_error() || sr.is_error()) {
                return true;
            }
            return false;
        }
        // Pointer and borrow coercion:
        // &T, &mut T, *mut T, or *T can initialize/assign to *T
        if (dest_d.kind == TypeKind::RawPtr) {
            if ((src_d.kind == TypeKind::Borrow || src_d.kind == TypeKind::MutBorrow ||
                 src_d.kind == TypeKind::RawPtr || src_d.kind == TypeKind::MutPtr) &&
                (dest_d.elem_type == src_d.elem_type || dest_d.elem_type.is_unknown() || src_d.elem_type.is_unknown() ||
                 dest_d.elem_type.is_foreign() || src_d.elem_type.is_foreign() ||
                 (is_integer(dest_d.elem_type) && is_integer(src_d.elem_type)))) {
                return true;
            }
        }
        // &mut T or *mut T can initialize/assign to *mut T
        if (dest_d.kind == TypeKind::MutPtr) {
            if ((src_d.kind == TypeKind::MutBorrow || src_d.kind == TypeKind::MutPtr) &&
                (dest_d.elem_type == src_d.elem_type || dest_d.elem_type.is_unknown() || src_d.elem_type.is_unknown() ||
                 dest_d.elem_type.is_foreign() || src_d.elem_type.is_foreign() ||
                 (is_integer(dest_d.elem_type) && is_integer(src_d.elem_type)))) {
                return true;
            }
        }
        // &mut T can initialize/assign where &T is expected
        if (dest_d.kind == TypeKind::Borrow && src_d.kind == TypeKind::MutBorrow &&
            (dest_d.elem_type == src_d.elem_type || dest_d.elem_type.is_unknown() || src_d.elem_type.is_unknown() ||
             dest_d.elem_type.is_foreign() || src_d.elem_type.is_foreign() ||
             (is_integer(dest_d.elem_type) && is_integer(src_d.elem_type)))) {
            return true;
        }
        return false;
    }

private:
    [[nodiscard]] static auto trim_view(std::string_view sv) noexcept -> std::string_view {
        while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
        while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) sv.remove_suffix(1);
        return sv;
    }

    template <typename Pred>
    [[nodiscard]] auto any_type_part(TypeId id, Pred&& pred) const -> bool {
        if (pred(id)) return true;
        if (id.index <= 19) return false;
        return any_type_part_in_desc(get(id), pred);
    }

    template <typename Pred>
    [[nodiscard]] auto any_type_part_in_desc(const TypeDesc& d, Pred&& pred) const -> bool {
        switch (d.kind) {
            case TypeKind::Slice:
            case TypeKind::Array:
            case TypeKind::Optional:
            case TypeKind::Fallible:
            case TypeKind::Own:
            case TypeKind::Shared:
            case TypeKind::Borrow:
            case TypeKind::MutBorrow:
            case TypeKind::RawPtr:
            case TypeKind::MutPtr:
                return any_type_part(d.elem_type, pred);
            case TypeKind::Map:
                return any_type_part(d.key_type, pred) || any_type_part(d.elem_type, pred);
            case TypeKind::Tuple:
                for (TypeId p : d.param_types) if (any_type_part(p, pred)) return true;
                return false;
            case TypeKind::Function:
                if (any_type_part(d.ret_type, pred)) return true;
                for (TypeId p : d.param_types) if (any_type_part(p, pred)) return true;
                return false;
            case TypeKind::GenericInst:
                for (TypeId a : d.type_args) if (any_type_part(a, pred)) return true;
                return false;
            default:
                return false;
        }
    }

    [[nodiscard]] auto contains_type_param_desc(const TypeDesc& d) const -> bool {
        return any_type_part_in_desc(d, [this](TypeId t) { return get(t).kind == TypeKind::TypeParam; });
    }

    [[nodiscard]] auto canonical_desc_key(const TypeDesc& d) const -> std::string {
        auto id_key = [this](TypeId id) -> std::string {
            return contains_type_param(id) ? ("#" + std::to_string(id.index)) : to_string(id);
        };
        switch (d.kind) {
            case TypeKind::Slice: return "slice:" + id_key(d.elem_type);
            case TypeKind::Array: return std::format("array:{}:{}", d.array_len, id_key(d.elem_type));
            case TypeKind::Map: return std::format("map:{}:{}", id_key(d.key_type), id_key(d.elem_type));
            case TypeKind::Optional: return "opt:" + id_key(d.elem_type);
            case TypeKind::Fallible: return "fall:" + id_key(d.elem_type);
            case TypeKind::Own: return "own:" + id_key(d.elem_type);
            case TypeKind::Shared: return "shared:" + id_key(d.elem_type);
            case TypeKind::Borrow: return "borrow:" + id_key(d.elem_type);
            case TypeKind::MutBorrow: return "mut_borrow:" + id_key(d.elem_type);
            case TypeKind::RawPtr: return "raw_ptr:" + id_key(d.elem_type);
            case TypeKind::MutPtr: return "mut_ptr:" + id_key(d.elem_type);
            case TypeKind::Tuple: {
                std::string k = "tuple:";
                for (std::size_t i = 0; i < d.param_types.size(); ++i) {
                    if (i > 0) k += ',';
                    k += id_key(d.param_types[i]);
                }
                return k;
            }
            case TypeKind::Function: {
                std::string k = "fn:" + id_key(d.ret_type) + ":(";
                for (std::size_t i = 0; i < d.param_types.size(); ++i) {
                    if (i > 0) k += ',';
                    k += id_key(d.param_types[i]);
                }
                k += ')';
                if (d.fn_min_params < d.param_types.size()) {
                    k += ":min=" + std::to_string(d.fn_min_params);
                }
                return k;
            }
            case TypeKind::GenericInst: {
                std::string k = "gen:" + d.base_name + "[";
                for (std::size_t i = 0; i < d.type_args.size(); ++i) {
                    if (i > 0) k += ',';
                    k += id_key(d.type_args[i]);
                }
                k += ']';
                return k;
            }
            default:
                return d.name;
        }
    }

    [[nodiscard]] auto intern_composite(TypeDesc d) const -> TypeId {
        if (contains_type_param_desc(d)) {
            std::string key = canonical_desc_key(d);
            if (auto it = param_structural_cache_.find(key); it != param_structural_cache_.end()) {
                return it->second;
            }
            if (d.name.empty()) d.name = format_desc(d);
            uint32_t next_id = static_cast<uint32_t>(types_.size());
            param_structural_cache_[key] = TypeId{next_id};
            types_.push_back(std::move(d));
            return TypeId{next_id};
        }
        if (d.name.empty()) d.name = format_desc(d);
        return intern(std::move(d));
    }

    std::string format_desc(const TypeDesc& d) const {
        if (!d.name.empty()) return d.name;
        switch (d.kind) {
            case TypeKind::Slice: return "[]" + to_string(d.elem_type);
            case TypeKind::Array: return std::format("[{}]{}", d.array_len, to_string(d.elem_type));
            case TypeKind::Map: return std::format("map[{}]{}", to_string(d.key_type), to_string(d.elem_type));
            case TypeKind::Optional: return to_string(d.elem_type) + "?";
            case TypeKind::Fallible: return to_string(d.elem_type) + "!";
            case TypeKind::Own: return std::format("own[{}]", to_string(d.elem_type));
            case TypeKind::Shared: return std::format("shared[{}]", to_string(d.elem_type));
            case TypeKind::Borrow: return "&" + to_string(d.elem_type);
            case TypeKind::MutBorrow: return "&mut " + to_string(d.elem_type);
            case TypeKind::RawPtr: return "*" + to_string(d.elem_type);
            case TypeKind::MutPtr: return "*mut " + to_string(d.elem_type);
            case TypeKind::Tuple: {
                std::string s = "(";
                for (std::size_t i = 0; i < d.param_types.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += to_string(d.param_types[i]);
                }
                s += ")";
                return s;
            }
            case TypeKind::Function: {
                std::string s = "fn(";
                for (std::size_t i = 0; i < d.param_types.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += to_string(d.param_types[i]);
                }
                s += ")";
                if (!d.ret_type.is_void() && !d.ret_type.is_unknown()) {
                    s += " -> " + to_string(d.ret_type);
                }
                return s;
            }
            case TypeKind::GenericInst: {
                std::string s = d.base_name + "[";
                for (std::size_t i = 0; i < d.type_args.size(); ++i) {
                    if (i > 0) s += ", ";
                    s += to_string(d.type_args[i]);
                }
                s += "]";
                return s;
            }
            default: return "unknown";
        }
    }

    mutable std::vector<TypeDesc> types_{};
    mutable std::unordered_map<std::string, TypeId> name_to_id_{};
    mutable std::unordered_map<std::string, TypeId> type_param_cache_{};
    mutable std::unordered_map<std::string, TypeId> param_structural_cache_{};
};

struct DeclVisInfo {
    std::string kind{}; // "struct", "enum", "interface", "fn", "global"
    std::string name{}; // unmangled symbol name
    std::string package_name{};
    std::string source_path{};
    int line = 1;
    bool is_pub = false;
};

struct UsageLedger {
    std::unordered_map<std::string, std::unordered_set<std::string>> used_qualifiers_by_file{};
    std::unordered_set<std::string> files_with_cpp_block{};
    std::unordered_set<std::string> used_types{};   // "pkg::orig_name"
    std::unordered_set<std::string> used_fns{};     // "pkg::fn_name"
    std::unordered_set<std::string> used_globals{}; // "pkg::gv_name"
    std::unordered_set<std::string> used_methods{}; // "orig_owner::method_name"
};

struct CheckedFile {
    FileAST ast{};
    TypeTable type_table{};
    // the struct, enum and interface names of the package, for resolving bare type names.
    std::unordered_set<std::string> declared_type_names{};
    std::unordered_map<std::string, FnDecl> fn_sigs{};
    std::unordered_map<std::string, FnDecl> methods{};     // "Type::method" -> FnDecl
    std::unordered_map<std::string, std::string> method_owner{}; // "method" -> "Type" (primary owner for single-owner methods)
    std::unordered_map<std::string, std::unordered_set<std::string>> method_owners{}; // "method" -> set of "Type"s
    std::unordered_map<std::string, EnumDecl> enums{};     // "EnumName" -> EnumDecl
    std::unordered_map<std::string, std::string> variant_to_enum{}; // "VariantName" -> "EnumName"
    std::unordered_set<std::string> ambiguous_variants{};
    std::unordered_set<std::string> colliding_fn_names{};
    std::unordered_set<std::string> colliding_gv_names{};
    std::unordered_map<std::string, std::vector<StructTagInfo>> struct_tags{};
    std::unordered_map<std::string, StructDecl> struct_defs{};
    std::unordered_map<std::string, DeclVisInfo> type_visibility{};
    std::unordered_map<std::string, DeclVisInfo> fn_visibility{};
    std::unordered_map<std::string, DeclVisInfo> gv_visibility{};
    mutable UsageLedger usage{};
    mutable std::unordered_set<std::string> reported_private_uses{};
    std::unordered_map<std::string, uint64_t> last_use_move{}; // "fnName:varName" -> expr_id
    std::unordered_map<std::string, std::vector<std::string>> cpp_fn_params{}; // C/C++ fn -> param QualTypes
    std::unordered_map<std::string, std::string> cpp_fn_returns{}; // C/C++ fn -> return QualType
    mutable bool has_cpp_interop{false};
    std::unordered_set<std::string> cpp_qualifiers{}; // "cpp" or aliases from `import cpp "..." as alias`
    std::unordered_map<std::string, std::string> cpp_alias_to_header{}; // alias -> header path
    std::unordered_map<std::string, std::string> core_aliases{}; // alias -> canonical_ns ("s" -> "str")
    std::unordered_map<std::string, std::string> vendor_aliases{}; // alias -> vendor name
    std::unordered_map<std::string, std::string> shadowed_default_qualifiers{}; // default ns -> active alias ("str" -> "s")
    std::unordered_set<std::string> interfaces{};
    std::unordered_map<std::string, InterfaceDecl> interface_defs{};
    std::unordered_set<std::string> move_only_structs{};
    // untyped `mut x:= <text>` declarations; codegen gives them an owned `std::string`
    // so a view initializer (a read-only `str` param, a slice) is copied rather than becoming a mutable view.
    // `mutable`: filled by the per-function checker, which holds the file by const reference.
    mutable std::unordered_set<const VarDeclStmt*> owned_text_decls{};
    // untyped `mut x:= <slice>` declarations, which own a copy when the initializer is a view.
    mutable std::unordered_set<const VarDeclStmt*> owned_slice_decls{};
    // untyped declarations of a `match` / `if` expression whose arms are integer constants: they are
    // `i64`, so codegen spells the type instead of `auto` (which deduced a 32-bit `int`).
    mutable std::unordered_set<const VarDeclStmt*> int64_decls{};
    // Non-enum bare identifier arms (`x => ...` / `x if ... => ...`) that bind the scrutinee value.
    mutable std::unordered_set<const MatchArm*> match_catchall_bindings{};
    // `g.value` on a `sync.MutexGuard[T]`, which codegen lowers to the guarded value (`*g` in C++).
    mutable std::unordered_set<const void*> guard_value_members{};
    // `diagnostics` is the source of truth; `errors` / `warnings` are its rendered text forms
    // (kept for callers that only print). Always add through add_error / add_warning.
    std::vector<std::string> warnings{};
    std::vector<std::string> errors{};
    std::vector<cfire::diag::StructuredDiagnostic> diagnostics{};

    void add_error(cfire::diag::StructuredDiagnostic d) {
        d.severity = "error";
        errors.push_back(cfire::diag::render_diagnostic(d));
        diagnostics.push_back(std::move(d));
    }
    void add_warning(cfire::diag::StructuredDiagnostic d) {
        d.severity = "warning";
        warnings.push_back(cfire::diag::render_diagnostic(d));
        diagnostics.push_back(std::move(d));
    }

    [[nodiscard]] static auto strip_pkg_mangle(std::string_view n) -> std::string {
        if (n.starts_with("cfire_pkg_")) {
            if (auto sep = n.find("__"); sep != std::string_view::npos) {
                return std::string(n.substr(sep + 2));
            }
        }
        return std::string(n);
    }

    [[nodiscard]] static auto extract_mangled_pkg(std::string_view n) -> std::string {
        if (n.starts_with("cfire_pkg_")) {
            if (auto sep = n.find("__"); sep != std::string_view::npos && sep > 10) {
                return std::string(n.substr(10, sep - 10));
            }
        }
        return {};
    }

    [[nodiscard]] auto normalize_pkg(std::string_view p) const -> std::string {
        if (!p.empty()) return std::string(p);
        return ast.package_name.empty() ? std::string("main") : ast.package_name;
    }

    [[nodiscard]] auto normalize_file_key(std::string_view f) const -> std::string {
        std::string raw = f.empty() ? ast.path : std::string(f);
        if (raw.empty()) return raw;
        return std::filesystem::path(raw).lexically_normal().string();
    }

    void mark_qualifier_used(std::string_view file, std::string_view qual) const {
        if (qual.empty()) return;
        usage.used_qualifiers_by_file[normalize_file_key(file)].insert(std::string(qual));
    }

    void mark_file_cpp_block(std::string_view file) const {
        usage.files_with_cpp_block.insert(normalize_file_key(file));
    }

    void mark_type_used(std::string_view decl_pkg, std::string_view name) const {
        std::string s = std::string(name);
        if (auto lb = s.find('['); lb != std::string::npos) s.resize(lb);
        std::string mp = extract_mangled_pkg(s);
        std::string orig = strip_pkg_mangle(s);
        std::string pkg = !mp.empty() ? mp : std::string(decl_pkg);
        if (auto dot = orig.find('.'); dot != std::string::npos) {
            if (pkg.empty()) pkg = orig.substr(0, dot);
            orig = orig.substr(dot + 1);
        }
        if (orig.empty()) return;
        usage.used_types.insert(normalize_pkg(pkg) + "::" + orig);
    }

    void mark_fn_used(std::string_view decl_pkg, std::string_view name) const {
        if (name.empty()) return;
        usage.used_fns.insert(normalize_pkg(decl_pkg) + "::" + std::string(name));
    }

    void mark_global_used(std::string_view decl_pkg, std::string_view name) const {
        if (name.empty()) return;
        usage.used_globals.insert(normalize_pkg(decl_pkg) + "::" + std::string(name));
    }

    void mark_method_used(std::string_view owner_type, std::string_view method_name) const {
        std::string base = std::string(owner_type);
        if (auto lb = base.find('['); lb != std::string::npos) base.resize(lb);
        std::string mp = extract_mangled_pkg(base);
        std::string orig = strip_pkg_mangle(base);
        if (auto dot = orig.find('.'); dot != std::string::npos) {
            if (mp.empty()) mp = orig.substr(0, dot);
            orig = orig.substr(dot + 1);
        }
        if (orig.empty() || method_name.empty()) return;
        if (!mp.empty()) {
            usage.used_methods.insert(mp + "." + orig + "::" + std::string(method_name));
        }
        usage.used_methods.insert(orig + "::" + std::string(method_name));
    }

    [[nodiscard]] auto lookup_type_vis(std::string_view raw_name, std::string_view cur_pkg) const -> const DeclVisInfo* {
        std::string s(raw_name);
        if (auto lb = s.find('['); lb != std::string::npos) s.resize(lb);
        for (auto p = s.find("::"); p != std::string::npos; p = s.find("::")) s.replace(p, 2, ".");
        if (s.empty()) return nullptr;
        std::string ncur = normalize_pkg(cur_pkg);
        if (s.find('.') == std::string::npos && !s.starts_with("cfire_pkg_")) {
            if (auto it = type_visibility.find(ncur + "." + s); it != type_visibility.end()) {
                return &it->second;
            }
        }
        if (auto it = type_visibility.find(s); it != type_visibility.end()) {
            return &it->second;
        }
        return nullptr;
    }
};

[[nodiscard]] inline auto find_interface_in_slice(const TypeRef& tr, const std::unordered_set<std::string>& ifaces) -> std::optional<std::string> {
    if ((tr.is_slice || tr.is_array) && tr.elem_type) {
        if (ifaces.contains(tr.elem_type->name)) {
            return tr.elem_type->name;
        }
        if (auto nested = find_interface_in_slice(*tr.elem_type, ifaces)) {
            return nested;
        }
    }
    if (tr.elem_type) {
        if (auto nested = find_interface_in_slice(*tr.elem_type, ifaces)) return nested;
    }
    if (tr.key_type) {
        if (auto nested = find_interface_in_slice(*tr.key_type, ifaces)) return nested;
    }
    for (const auto& ta : tr.type_args) {
        if (auto nested = find_interface_in_slice(ta, ifaces)) return nested;
    }
    for (const auto& tt : tr.tuple_types) {
        if (auto nested = find_interface_in_slice(tt, ifaces)) return nested;
    }
    return std::nullopt;
}

[[nodiscard]] inline auto trim_str(std::string_view sv) -> std::string {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) sv.remove_suffix(1);
    return std::string(sv);
}

} // namespace cfire::frontend
