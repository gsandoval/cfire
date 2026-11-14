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
    NominalStruct,
    NominalEnum,
    Interface,
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
    // Q39 (CF-183): `byte` is an alias of `u8` (slot 17 is kept so the builtin ids stay stable).
    static constexpr TypeId Byte() noexcept { return TypeId{8}; }
};

struct TypeDesc {
    TypeKind kind{TypeKind::Unknown};
    std::string name{};
    std::string package_name{};
    TypeId elem_type{TypeId::Unknown()};
    TypeId key_type{TypeId::Unknown()};
    std::vector<TypeId> param_types{};
    TypeId ret_type{TypeId::Unknown()};
    std::size_t array_len{0};
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

    [[nodiscard]] auto from_name(std::string_view name) const -> TypeId {
        if (name.empty()) return TypeId::Unknown();
        std::string n(name);
        if (auto it = name_to_id_.find(n); it != name_to_id_.end()) {
            return it->second;
        }
        if (n.starts_with("[]")) {
            TypeId elem = from_name(n.substr(2));
            return make_slice(elem);
        }
        // `map[K]V` (K may itself contain brackets) and `[N]T`.
        if (n.starts_with("map[") && !n.ends_with('?') && !n.ends_with('!')) {
            int depth = 0;
            for (std::size_t i = 3; i < n.size(); ++i) {
                if (n[i] == '[') ++depth;
                else if (n[i] == ']' && --depth == 0) {
                    TypeId key = from_name(n.substr(4, i - 4));
                    TypeId val = from_name(n.substr(i + 1));
                    if (key.is_unknown() || val.is_unknown()) return TypeId::Unknown();
                    return make_map(key, val);
                }
            }
        }
        if (n.size() > 2 && n[0] == '[' && std::isdigit(static_cast<unsigned char>(n[1])) && !n.ends_with('?') && !n.ends_with('!')) {
            if (auto rb = n.find(']'); rb != std::string::npos) {
                std::size_t len = 0;
                for (std::size_t i = 1; i < rb; ++i) {
                    if (!std::isdigit(static_cast<unsigned char>(n[i]))) return TypeId::Unknown();
                    len = len * 10 + static_cast<std::size_t>(n[i] - '0');
                }
                TypeId elem = from_name(n.substr(rb + 1));
                if (!elem.is_unknown()) return make_array(elem, len);
            }
        }
        if (n.ends_with('?')) {
            TypeId inner = from_name(n.substr(0, n.size() - 1));
            return make_optional(inner);
        }
        if (n.ends_with('!')) {
            TypeId inner = from_name(n.substr(0, n.size() - 1));
            return make_fallible(inner);
        }
        if (n.starts_with("&mut ")) {
            TypeId inner = from_name(n.substr(5));
            return make_mut_borrow(inner);
        }
        if (n.starts_with('&')) {
            TypeId inner = from_name(n.substr(1));
            return make_borrow(inner);
        }
        if (n.starts_with("*mut ")) {
            TypeId inner = from_name(n.substr(5));
            return make_mut_ptr(inner);
        }
        if (n.starts_with('*')) {
            TypeId inner = from_name(n.substr(1));
            return make_raw_ptr(inner);
        }
        if (n.starts_with("own[") && n.ends_with(']')) {
            TypeId inner = from_name(n.substr(4, n.size() - 5));
            return make_own(inner);
        }
        if (n.starts_with("shared[") && n.ends_with(']')) {
            TypeId inner = from_name(n.substr(7, n.size() - 8));
            return make_shared(inner);
        }
        TypeDesc d;
        d.kind = TypeKind::NominalStruct;
        d.name = n;
        return intern(std::move(d));
    }

    [[nodiscard]] auto from_typeref(const TypeRef& tr) const -> TypeId {
        if (tr.is_fallible) {
            TypeRef inner = tr;
            inner.is_fallible = false;
            return make_fallible(from_typeref(inner));
        }
        if (tr.is_optional) {
            TypeRef inner = tr;
            inner.is_optional = false;
            return make_optional(from_typeref(inner));
        }
        if (tr.is_slice) {
            TypeId elem = tr.elem_type ? from_typeref(*tr.elem_type) : from_name(tr.name.starts_with("[]") ? tr.name.substr(2) : tr.name);
            return make_slice(elem);
        }
        if (tr.is_array) {
            TypeId elem = tr.elem_type ? from_typeref(*tr.elem_type) : from_name(tr.name);
            std::size_t len = 0;
            if (!tr.array_len.empty()) {
                try { len = std::stoul(tr.array_len); } catch (...) {}
            }
            return make_array(elem, len);
        }
        if (tr.is_map) {
            TypeId k = tr.key_type ? from_typeref(*tr.key_type) : TypeId::Str();
            TypeId v = tr.elem_type ? from_typeref(*tr.elem_type) : TypeId::Unknown();
            return make_map(k, v);
        }
        if (!tr.smart_wrap.empty()) {
            TypeId elem = tr.elem_type ? from_typeref(*tr.elem_type) : from_name(tr.name);
            if (tr.smart_wrap == "own") return make_own(elem);
            if (tr.smart_wrap == "shared") return make_shared(elem);
        }
        if (tr.is_borrow) {
            TypeId elem = tr.elem_type ? from_typeref(*tr.elem_type) : from_name(tr.name);
            return make_borrow(elem);
        }
        if (tr.is_mut_borrow) {
            TypeId elem = tr.elem_type ? from_typeref(*tr.elem_type) : from_name(tr.name);
            return make_mut_borrow(elem);
        }
        if (tr.is_ptr) {
            TypeId elem = tr.elem_type ? from_typeref(*tr.elem_type) : from_name(tr.name);
            return make_raw_ptr(elem);
        }
        if (tr.is_mut_ptr) {
            TypeId elem = tr.elem_type ? from_typeref(*tr.elem_type) : from_name(tr.name);
            return make_mut_ptr(elem);
        }
        // Q44 (CF-183): a stdlib priority queue keeps its element type (`collections.MinQueue[i64]`), so its
        // `pop() -> T?` is typed. Other generic types are still named without their arguments.
        if (tr.type_args.size() == 1 && (tr.name.ends_with("PriorityQueue") || tr.name.ends_with("MinQueue"))) {
            return from_name(tr.name + "[" + to_string(from_typeref(tr.type_args[0])) + "]");
        }
        return from_name(tr.name);
    }

    [[nodiscard]] auto make_slice(TypeId elem) const -> TypeId {
        std::string n = "[]" + to_string(elem);
        if (auto it = name_to_id_.find(n); it != name_to_id_.end()) return it->second;
        TypeDesc d;
        d.kind = TypeKind::Slice;
        d.name = n;
        d.elem_type = elem;
        return intern(std::move(d));
    }

    [[nodiscard]] auto make_array(TypeId elem, std::size_t len) const -> TypeId {
        std::string n = std::format("[{}]{}", len, to_string(elem));
        if (auto it = name_to_id_.find(n); it != name_to_id_.end()) return it->second;
        TypeDesc d;
        d.kind = TypeKind::Array;
        d.name = n;
        d.elem_type = elem;
        d.array_len = len;
        return intern(std::move(d));
    }

    [[nodiscard]] auto make_map(TypeId key, TypeId val) const -> TypeId {
        std::string n = std::format("map[{}]{}", to_string(key), to_string(val));
        if (auto it = name_to_id_.find(n); it != name_to_id_.end()) return it->second;
        TypeDesc d;
        d.kind = TypeKind::Map;
        d.name = n;
        d.key_type = key;
        d.elem_type = val;
        return intern(std::move(d));
    }

    [[nodiscard]] auto make_optional(TypeId base) const -> TypeId {
        if (base.is_unknown()) return TypeId::Unknown();
        std::string n = to_string(base) + "?";
        if (auto it = name_to_id_.find(n); it != name_to_id_.end()) return it->second;
        TypeDesc d;
        d.kind = TypeKind::Optional;
        d.name = n;
        d.elem_type = base;
        return intern(std::move(d));
    }

    [[nodiscard]] auto make_fallible(TypeId base) const -> TypeId {
        if (base.is_unknown()) return TypeId::Unknown();
        std::string n = to_string(base) + "!";
        if (auto it = name_to_id_.find(n); it != name_to_id_.end()) return it->second;
        TypeDesc d;
        d.kind = TypeKind::Fallible;
        d.name = n;
        d.elem_type = base;
        return intern(std::move(d));
    }

    [[nodiscard]] auto make_own(TypeId base) const -> TypeId {
        std::string n = std::format("own[{}]", to_string(base));
        if (auto it = name_to_id_.find(n); it != name_to_id_.end()) return it->second;
        TypeDesc d;
        d.kind = TypeKind::Own;
        d.name = n;
        d.elem_type = base;
        return intern(std::move(d));
    }

    [[nodiscard]] auto make_shared(TypeId base) const -> TypeId {
        std::string n = std::format("shared[{}]", to_string(base));
        if (auto it = name_to_id_.find(n); it != name_to_id_.end()) return it->second;
        TypeDesc d;
        d.kind = TypeKind::Shared;
        d.name = n;
        d.elem_type = base;
        return intern(std::move(d));
    }

    [[nodiscard]] auto make_borrow(TypeId base) const -> TypeId {
        std::string n = "&" + to_string(base);
        if (auto it = name_to_id_.find(n); it != name_to_id_.end()) return it->second;
        TypeDesc d;
        d.kind = TypeKind::Borrow;
        d.name = n;
        d.elem_type = base;
        return intern(std::move(d));
    }

    [[nodiscard]] auto make_mut_borrow(TypeId base) const -> TypeId {
        std::string n = "&mut " + to_string(base);
        if (auto it = name_to_id_.find(n); it != name_to_id_.end()) return it->second;
        TypeDesc d;
        d.kind = TypeKind::MutBorrow;
        d.name = n;
        d.elem_type = base;
        return intern(std::move(d));
    }

    [[nodiscard]] auto make_raw_ptr(TypeId base) const -> TypeId {
        std::string n = "*" + to_string(base);
        if (auto it = name_to_id_.find(n); it != name_to_id_.end()) return it->second;
        TypeDesc d;
        d.kind = TypeKind::RawPtr;
        d.name = n;
        d.elem_type = base;
        return intern(std::move(d));
    }

    [[nodiscard]] auto make_mut_ptr(TypeId base) const -> TypeId {
        std::string n = "*mut " + to_string(base);
        if (auto it = name_to_id_.find(n); it != name_to_id_.end()) return it->second;
        TypeDesc d;
        d.kind = TypeKind::MutPtr;
        d.name = n;
        d.elem_type = base;
        return intern(std::move(d));
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

    [[nodiscard]] auto is_float(TypeId id) const -> bool {
        const auto& d = get(id);
        return d.kind == TypeKind::Float32 || d.kind == TypeKind::Float64;
    }

    // CF-196: (bits, signed) of an integer type; {0, false} for anything else. `isize`/`usize` are 64-bit.
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

    // CF-196: every value of integer type `src` fits in `dest` (Q9: only these convert implicitly).
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
        if (dest.is_unknown() || src.is_unknown()) return false;
        const auto& dest_d = get(dest);
        const auto& src_d = get(src);
        if (dest_d.kind == TypeKind::Optional && (dest_d.elem_type == src || is_assignable(dest_d.elem_type, src))) return true;
        if (dest_d.kind == TypeKind::Fallible && (dest_d.elem_type == src || is_assignable(dest_d.elem_type, src) || src_d.name == "Error" || src_d.name == "cfire::Error")) return true;
        if (is_integer(dest) && is_integer(src)) return true;
        if (is_float(dest) && is_numeric(src)) return true;
        if (dest == make_borrow_const(TypeId::Str()) && src == TypeId::Str()) return true;
        // Q36 (CF-182): a `&str` view is copied implicitly wherever an owned `str` is required.
        if (dest == TypeId::Str() && src == make_borrow_const(TypeId::Str())) return true;
        if (dest_d.kind == TypeKind::Borrow && dest_d.elem_type == src) return true;
        if (dest_d.kind == TypeKind::Slice && src_d.kind == TypeKind::Array && dest_d.elem_type == src_d.elem_type) return true;
        // Pointer and borrow coercion:
        // &T, &mut T, *mut T, or *T can initialize/assign to *T
        if (dest_d.kind == TypeKind::RawPtr) {
            if ((src_d.kind == TypeKind::Borrow || src_d.kind == TypeKind::MutBorrow ||
                 src_d.kind == TypeKind::RawPtr || src_d.kind == TypeKind::MutPtr) &&
                (dest_d.elem_type == src_d.elem_type || dest_d.elem_type.is_unknown() || src_d.elem_type.is_unknown() ||
                 (is_integer(dest_d.elem_type) && is_integer(src_d.elem_type)))) {
                return true;
            }
        }
        // &mut T or *mut T can initialize/assign to *mut T
        if (dest_d.kind == TypeKind::MutPtr) {
            if ((src_d.kind == TypeKind::MutBorrow || src_d.kind == TypeKind::MutPtr) &&
                (dest_d.elem_type == src_d.elem_type || dest_d.elem_type.is_unknown() || src_d.elem_type.is_unknown() ||
                 (is_integer(dest_d.elem_type) && is_integer(src_d.elem_type)))) {
                return true;
            }
        }
        // &mut T can initialize/assign where &T is expected
        if (dest_d.kind == TypeKind::Borrow && src_d.kind == TypeKind::MutBorrow &&
            (dest_d.elem_type == src_d.elem_type || dest_d.elem_type.is_unknown() || src_d.elem_type.is_unknown() ||
             (is_integer(dest_d.elem_type) && is_integer(src_d.elem_type)))) {
            return true;
        }
        return false;
    }

private:
    [[nodiscard]] auto make_borrow_const(TypeId base) const -> TypeId {
        std::string n = "&" + to_string(base);
        if (auto it = name_to_id_.find(n); it != name_to_id_.end()) return it->second;
        return TypeId::Unknown();
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
            default: return "unknown";
        }
    }

    mutable std::vector<TypeDesc> types_{};
    mutable std::unordered_map<std::string, TypeId> name_to_id_{};
};

struct CheckedFile {
    FileAST ast{};
    TypeTable type_table{};
    // CF-190: the struct, enum and interface names of the package, for resolving bare type names.
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
    std::unordered_map<std::string, uint64_t> last_use_move{}; // "fnName:varName" -> expr_id
    std::unordered_map<std::string, std::vector<std::string>> cpp_fn_params{}; // C/C++ fn -> param QualTypes
    std::unordered_set<std::string> cpp_qualifiers{}; // "cpp" or aliases from `import cpp "..." as alias`
    std::unordered_map<std::string, std::string> cpp_alias_to_header{}; // alias -> header path
    std::unordered_map<std::string, std::string> core_aliases{}; // alias -> canonical_ns ("s" -> "str")
    std::unordered_map<std::string, std::string> vendor_aliases{}; // alias -> vendor name
    std::unordered_map<std::string, std::string> shadowed_default_qualifiers{}; // default ns -> active alias ("str" -> "s")
    std::unordered_set<std::string> interfaces{};
    // Q36 (CF-182): untyped `mut x := <text>` declarations; codegen gives them an owned `std::string`
    // so a view initializer (a read-only `str` param, a slice) is copied rather than becoming a mutable view.
    // `mutable`: filled by the per-function checker, which holds the file by const reference.
    mutable std::unordered_set<const VarDeclStmt*> owned_text_decls{};
    // Q53 (CF-189): untyped `mut x := <slice>` declarations, which own a copy when the initializer is a view.
    mutable std::unordered_set<const VarDeclStmt*> owned_slice_decls{};
    // CF-196: untyped declarations of a `match` / `if` expression whose arms are integer constants: they are
    // `i64`, so codegen spells the type instead of `auto` (which deduced a 32-bit `int`).
    mutable std::unordered_set<const VarDeclStmt*> int64_decls{};
    // Q52 (CF-189): `g.value` on a `sync.MutexGuard[T]`, which codegen lowers to the guarded value (`*g` in C++).
    mutable std::unordered_set<const void*> guard_value_members{};
    // Q24 (CF-149): `diagnostics` is the source of truth; `errors` / `warnings` are its rendered text forms
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
