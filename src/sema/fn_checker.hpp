#pragma once
// src/sema/fn_checker.hpp — Single-Pass Function Body & Effect Checker

#include "types.hpp"
#include "stdlib_index.hpp"
#include "builtin_methods.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cfire::frontend {

#ifdef CFIRE_SPLIT_TUS
#define CFIRE_SEMA_INLINE
#else
#define CFIRE_SEMA_INLINE inline
#endif

// why `<ns>.<name>` in a type position is not a stdlib type, or "" when it is one. `ns` is the module's
// canonical namespace (`http`, `sqlite`); `in_build_manifest` is true in a `build.cf` file, the only place where
// the `build.*` types exist. Without this check clang rejected the generated C++ (cfire/TYPE-ERROR).
[[nodiscard]] CFIRE_SEMA_INLINE auto stdlib_type_name_problem(std::string_view ns, std::string_view name, bool in_build_manifest) -> std::string;


// why a bare type name (no module qualifier) does not resolve, or "" when it does. It resolves to a
// builtin, a struct / enum / interface of the package, or a type parameter in scope.
[[nodiscard]] CFIRE_SEMA_INLINE auto bare_type_name_problem(std::string_view name, const std::unordered_set<std::string>& declared,
                                                 const std::unordered_set<std::string>& type_params) -> std::string;

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
                  std::vector<std::string>& warnings, std::vector<std::string>& errors,
                  std::vector<cfire::diag::StructuredDiagnostic>& diagnostics, bool resilient = false);

    SemaFnChecker(std::string_view path, const FnDecl& fn, const CheckedFile& cf, std::vector<std::string>& warnings)
        : SemaFnChecker(path, fn, cf, warnings, fallback_errors_, fallback_diags_, false) {}

    std::unordered_map<std::string, uint64_t> last_read_expr_id{};
    std::unordered_set<std::string> read_in_loop_{};

    // diagnostics are recorded structurally where they are detected. In non-resilient mode the
    // rendered text is also returned so callers can stop early.
    [[nodiscard]] auto emit_error(cfire::diag::StructuredDiagnostic d) -> std::expected<void, std::string> {
        d.severity = "error";
        std::string text = cfire::diag::render_diagnostic(d);
        errors_.push_back(text);
        diags_.push_back(std::move(d));
        if (resilient_) return {};
        return std::unexpected(std::move(text));
    }
    void emit_warning(cfire::diag::StructuredDiagnostic d) {
        d.severity = "warning";
        warnings_.push_back(cfire::diag::render_diagnostic(d));
        diags_.push_back(std::move(d));
    }
    [[nodiscard]] auto diag_at(int line, int col, std::string_view code, std::string msg,
                               std::unordered_map<std::string, std::string> facts = {}) const -> cfire::diag::StructuredDiagnostic {
        return cfire::diag::diag(path_, line, col, code, std::move(msg), std::move(facts));
    }

    void invalidate_loans_for_owner(const std::string& owner, std::string_view reason, int line);

    // Records every owner `e` borrows from onto `name` (the first stays in borrow_origin_ for existing checks).
    void record_borrow_origins(const std::string& name, const ExprPtr& e);

    // (A3): does `call` produce a temporary owned value (as opposed to a view/pointer into storage
    // that outlives the expression)? Unknown callees are treated as owning: cfire values are owned unless
    // a signature says `&T` / `*T`.
    [[nodiscard]] auto call_returns_owned_temporary(const CallExpr& call) const -> bool;

    // If `e` creates a view or pointer (`x[a..b]`, `&x`, `x.data()`) whose base object is a temporary owned
    // value, the view dangles at the end of the full expression once it is stored (A3).
    [[nodiscard]] auto view_of_temporary(const ExprPtr& e) const -> bool;

    [[nodiscard]] auto check_view_of_temporary(const ExprPtr& e, std::string_view target, int line, int col)
        -> std::expected<void, std::string>;

    [[nodiscard]] static auto returns_view_type(const TypeRef& t) -> bool;

    // all owners an expression's value borrows from.
    //  - `&x`, `x[a..b]`, `x.data()`: the root owner of x (existing rule, see extract_borrow_source)
    //  - a call whose declared return type is a view or pointer borrows from its receiver and from every
    //    argument that is (or borrows from) a local path: `h := head(s)` keeps `s` loaned while `h` lives
    //  - a struct literal borrows from every owner its field values borrow from
    [[nodiscard]] auto extract_borrow_sources(const ExprPtr& e) const -> std::vector<std::string>;

    [[nodiscard]] auto check_not_iterated_collection(const std::string& root, int line, int col)
        -> std::expected<void, std::string>;

    [[nodiscard]] auto record_var_read(const std::string& name, int line, int col, uint64_t expr_id = 0)
        -> std::expected<void, std::string>;

    struct PathInfo {
        std::string root{};
        std::string path{};
    };

    [[nodiscard]] static auto extract_path_info(const ExprPtr& e) -> std::optional<PathInfo>;

    [[nodiscard]] auto resolve_root_owner(const std::string& var) const -> std::string;

    [[nodiscard]] auto extract_borrow_source(const ExprPtr& e, bool* is_direct_expr = nullptr) const -> std::optional<std::string>;

    [[nodiscard]] auto is_ptr_or_smart_init(const ExprPtr& e) const -> bool;

    [[nodiscard]] static auto is_const_zero_expr(const ExprPtr& e) -> bool;

    [[nodiscard]] static auto is_dynamic_string_expr(const ExprPtr& e) -> bool;

    // SQL statements reach `.exec` / `.query` on SQLite connections, statements and transactions.
    [[nodiscard]] auto is_sql_receiver(const ExprPtr& obj) const -> bool;

    // is `e` a string the program fully controls? Literals without interpolation, concatenations of
    // constants, and locals whose every assignment was constant. Anything else (parameters, call results,
    // interpolated or concatenated runtime data) may carry external input into a shell or SQL statement.
    [[nodiscard]] auto is_constant_string_expr(const ExprPtr& e) const -> bool;

    [[nodiscard]] auto is_cpp_qualifier_sema(std::string_view name) const -> bool;

    [[nodiscard]] auto is_vendor_alias_sema(std::string_view name) const -> bool;

    [[nodiscard]] auto resolve_stdlib_ns_sema(std::string_view qualifier) const -> std::string;

    [[nodiscard]] auto is_namespace_ident_sema(std::string_view name) const -> bool;

    [[nodiscard]] static auto is_common_stdlib_method_sema(std::string_view name) -> bool;

    [[nodiscard]] auto resolve_member_call_sig(const MemberExpr& mem) const -> const FnDecl*;

    [[nodiscard]] auto resolve_interface_method_sig_sema(const MemberExpr& mem) const -> const InterfaceMethodSig*;

    [[nodiscard]] auto is_expr_type_unresolved_for_redundancy_sema(const ExprPtr& inner) const -> bool;

    [[nodiscard]] auto resolve_enum_by_prefix_sema(std::string_view prefix) const -> const EnumDecl*;

    [[nodiscard]] auto expr_returns_optional_sema(const ExprPtr& e) const -> bool;

    // the former string-based inference, now producing TypeIds. Only reached as the fallback
    // of infer_expr_type_id_sema; names for messages come from type_name_of().
    [[nodiscard]] auto infer_named_type_id_(const ExprPtr& e) const -> TypeId;

    [[nodiscard]] auto infer_expr_type_id_sema(const ExprPtr& e) const -> TypeId;

    // Display / lookup name of an expression's type, derived from its TypeId ("" when unknown).
    [[nodiscard]] auto type_name_of(const ExprPtr& e) const -> std::string;

    // `byte` is `u8`, so a literal in 0..=255 or a `u8` value goes into a `[]byte` as is; a wider
    // integer needs an explicit `byte(x)`, so a push never truncates silently.
    [[nodiscard]] auto is_non_byte_int_or_u8_expr(const ExprPtr& e) const -> bool;

    [[nodiscard]] auto is_byte_slice_expr(const ExprPtr& e) const -> bool;

    [[nodiscard]] auto infer_expr_null_state(const ExprPtr& e) const -> std::optional<NullState>;

    [[nodiscard]] static auto extract_null_check(const ExprPtr& cond) -> std::optional<std::pair<std::string, bool>>;

    [[nodiscard]] static auto paths_conflict(std::string_view a, std::string_view b) -> bool;

    [[nodiscard]] static auto expr_start_loc(const ExprPtr& e) -> std::pair<int, int>;

    [[nodiscard]] static auto format_type_ref_sema(const TypeRef& t) -> std::string;

    [[nodiscard]] auto is_stdlib_ns_imported(std::string_view ns) const -> bool;

    // is `t` a type Sema fully understands (so a mismatch is certain, not a gap in inference)?
    // Primitives, str, &str, slices/arrays of those, and nominal types declared in this program.
    [[nodiscard]] auto is_concrete_checkable_type(TypeId t) const -> bool;

    // a type argument bound to `T: Iface` must implement every method of Iface (name, parameter
    // count and types, return type). Concepts used to be emitted without being checked anywhere.
    [[nodiscard]] auto check_interface_constraints(const CallExpr& call, const FnDecl& sig, int line, int col)
        -> std::expected<void, std::string>;

    // the column where `e` starts in the source (a binary or postfix node points at its operator).
    [[nodiscard]] static auto leftmost_col(const ExprPtr& e) -> int;

    // the value of an integer constant expression (literals combined with `+ - * / % << >> & | ^` and
    // unary `-`); nullopt for anything else, or when it leaves the 64-bit range.
    [[nodiscard]] static auto const_int_value(const ExprPtr& e) -> std::optional<__int128>;

    // the integer type of `e` for the narrowing check. A literal operand takes the other operand's type
    // (`8080 + i32(i)` is `i32`, as in C++), and a shift has its left operand's type.
    [[nodiscard]] auto int_value_type(const ExprPtr& e) -> TypeId;

    // checks a value against the type it initializes (an argument, a field, a declaration, an
    // assignment, a return). An array literal is checked element by element against the element type, an
    // integer literal against the target's range, and an integer value must widen: narrowing or a sign change
    // needs an explicit, checked `T(x)`. `handled` says this check decided the case, so the caller skips
    // its own whole-type comparison.
    [[nodiscard]] auto check_value_for(TypeId want, const ExprPtr& e, bool& handled) -> std::expected<void, std::string>;

    // argument types of calls to functions declared in this program (clang used to report these).
    [[nodiscard]] auto check_call_argument_types(const CallExpr& call, const FnDecl& sig) -> std::expected<void, std::string>;

    // a constant shift count outside [0, bit width of the promoted left operand) is rejected.
    [[nodiscard]] auto check_const_shift_count(const ExprPtr& left, const ExprPtr& count, int line, int col)
        -> std::expected<void, std::string>;

    // an integer constant that does not fit its target type is a compile error, not a silent
    // truncation (`a: u8 = 300`, `u8(300)`, `c: u32 = -1`). Runtime values are range-checked by `T(x)`.
    [[nodiscard]] auto check_int_literal_fits(const ExprPtr& e, std::string_view tname, int line, int col)
        -> std::expected<void, std::string>;

    // methods on builtin types are a closed set. Anything else would fall through to the raw
    // std::string / std::vector / map member of the same name, which bypasses intents and the loan tracker.
    enum class BuiltinReceiver : std::uint8_t { None, Str, Slice, Array, Map };

    [[nodiscard]] static auto classify_builtin_receiver(std::string_view t) -> BuiltinReceiver;

    [[nodiscard]] static constexpr auto bm_bits_of(BuiltinReceiver k) -> std::uint8_t {
        switch (k) {
            case BuiltinReceiver::Str: return BmStr;
            case BuiltinReceiver::Slice: return BmSlice;
            case BuiltinReceiver::Array: return BmArray;
            case BuiltinReceiver::Map: return BmMap;
            case BuiltinReceiver::None: return 0;
        }
        return 0;
    }

    // the receiver kind of a builtin method call, from the receiver's TypeId (borrows and pointers
    // auto-deref). Falls back to the receiver's spelled type when inference has no TypeId.
    [[nodiscard]] auto builtin_recv_bits(const ExprPtr& obj) const -> std::uint8_t;

    [[nodiscard]] auto builtin_self_type(const ExprPtr& obj) const -> TypeId;

    // Resolves a builtin-method type spelling (`Self`, `T?`, `[]K`, `&str?`, `usize`) against the receiver.
    [[nodiscard]] auto resolve_builtin_type(std::string_view spec, TypeId self) const -> TypeId;

    [[nodiscard]] auto lookup_builtin_method(const MemberExpr& mem) const -> const BuiltinMethod*;

    [[nodiscard]] static auto builtin_method_allowed(BuiltinReceiver k, std::string_view m) -> bool;

    // arity, argument types and `mut` receivers of builtin method calls, all read from the table.
    [[nodiscard]] auto check_builtin_method_call(const CallExpr& call, const MemberExpr& mem, int line, int col) -> std::expected<void, std::string>;

    // Canonical cfire spelling for raw C++ members, used for the diagnostic hint and `cfire fix`.
    [[nodiscard]] static auto builtin_method_replacement(BuiltinReceiver k, std::string_view m) -> std::string_view;

    [[nodiscard]] static auto is_math_constant(std::string_view f) -> bool {
        return f == "PI" || f == "E" || f == "TAU" || f == "SQRT_2" || f == "LN_2" || f == "LN_10";
    }

    [[nodiscard]] static auto bm_kind_name(std::uint8_t bits) -> std::string_view {
        if (bits & BmStr) return "str";
        if (bits & BmSlice) return "slice";
        if (bits & BmArray) return "fixed array";
        if (bits & BmMap) return "map";
        if (bits & BmInt) return "integer";
        if (bits & BmFloat) return "float";
        if (bits & BmBool) return "bool";
        return "value";
    }

    [[nodiscard]] static auto is_numeric_literal_expr(const ExprPtr& e) -> bool;

    [[nodiscard]] auto check_builtin_method_surface(const MemberExpr& mem, int line, int col) -> std::expected<void, std::string>;

    [[nodiscard]] auto check_stdlib_ns_imported(std::string_view ns, int line, int col) -> std::expected<void, std::string>;

    // `local_decl`: a local variable's type, which analyze.hpp's declaration pass does not see (stdlib type names).
    [[nodiscard]] auto check_type_ref_separators(const TypeRef& tr, int line, int col, bool local_decl = false) -> std::expected<void, std::string>;

    [[nodiscard]] auto is_ident_in_scope_sema(const std::string& ident) const -> bool;

    [[nodiscard]] static auto is_builtin_ident_sema(std::string_view name) -> bool;

    [[nodiscard]] auto is_known_identifier_sema(const std::string& name) const -> bool;

    // ---------------------------------------------------------------------------------
    // Canonical nominal type identity (canonical type identity)
    //
    // '.' qualifies a package (`webui.App`, `rl.Rectangle`); '::' walks type-level
    // paths (`pkg.Type::Variant`, `Enum::Variant`). The same source type reaches sema in
    // different spellings: the parser joins annotations with '::' (`rl::Rectangle`),
    // struct literals keep the source separator (`rl.Rectangle`), and stdlib catalog
    // signatures are written from inside their module (`App`). Every name -> TypeId
    // conversion goes through `type_id_of`, which restores the package boundary by
    // *resolution* (is the leading segment a package/alias in this file?), never by
    // blind separator rewriting. The canonical key uses the import alias as written.
    // ---------------------------------------------------------------------------------

    [[nodiscard]] auto is_package_qualifier_sema(std::string_view head) const -> bool;

    /// Canonicalizes a bare (unwrapped) nominal path: `rl::Rectangle` -> `rl.Rectangle`,
    /// `webui.App` -> `webui.App`, `Enum::Variant` / local `App` unchanged.
    [[nodiscard]] auto canonical_nominal_path_sema(std::string_view name) const -> std::string;

    /// Canonicalizes a full type spelling, recursing through the same wrappers that
    /// `TypeTable::from_name` decomposes.
    [[nodiscard]] auto canonical_type_name_sema(std::string_view name) const -> std::string;

    /// Canonicalizes every nominal leaf of a parsed TypeRef (annotations, fields, params).
    [[nodiscard]] auto canonical_typeref_sema(TypeRef tr) const -> TypeRef;

    /// Single entry point for converting a type spelling into an interned TypeId.
    /// Strict: used for explicit annotations, which must be spelled as canonical type identity requires.
    [[nodiscard]] auto type_id_of(std::string_view name) const -> TypeId;

    [[nodiscard]] auto type_id_of(const TypeRef& tr) const -> TypeId;

    /// Hint: explain local-vs-package mismatches that share a leaf name.
    [[nodiscard]] auto nominal_mismatch_hint_sema(TypeId a, TypeId b) const -> std::string;

    /// The qualifier this file uses for a stdlib/vendor module: its import alias if one
    /// was given, otherwise the module's own namespace.
    [[nodiscard]] auto local_qualifier_for_module_sema(std::string_view module_import, std::string_view default_ns) const -> std::string;

    /// Stdlib catalog signatures name module-local types bare (`-> App`). Qualify such a
    /// return type with the module's qualifier in this file (`webui.App`), using the
    /// metadata table as the source of truth for which names a module declares.
    [[nodiscard]] auto qualify_stdlib_type_sema(const StdFnSigInfo& sig, std::string_view qualifier) const -> std::string;

    [[nodiscard]] auto resolve_ident(const std::string& name) const -> std::optional<Resolution>;

    [[nodiscard]] auto resolve_member(const MemberExpr& mem) const -> std::optional<Resolution>;

    [[nodiscard]] auto expr_returns_fallible_sema(const ExprPtr& e, std::string* out_callee = nullptr, std::string* out_ret = nullptr) const -> bool;

    [[nodiscard]] auto is_expr_fallible_for_unwrap_sema(const ExprPtr& e) const -> bool;

    [[nodiscard]] auto is_expr_optional_for_unwrap_sema(const ExprPtr& e) const -> bool;

    [[nodiscard]] auto validate_move_operand(
        const ExprPtr& operand,
        bool required_by_sig,
        int line,
        int col
    ) -> std::expected<void, std::string>;

    [[nodiscard]] static auto is_effectful_stmt_expr(const ExprPtr& e) -> bool;

    [[nodiscard]] static auto extract_root_ident_sema(const ExprPtr& e) -> std::string;

    [[nodiscard]] auto check_expr(const ExprPtr& e) -> std::expected<void, std::string>;

    // both branches of an `if` expression yield the same type. A bare numeric literal adapts to
    // the other branch's numeric type (`if c { n } else { 0 }`).
    std::unordered_set<const MatchExpr*> if_exprs_checked_;  // an initializer can be checked twice

    [[nodiscard]] auto check_if_expr_branches(const MatchExpr& me) -> std::expected<void, std::string>;

    // Primitive type shared by all arms of a match expression, treating bare literal arms as adaptable.
    [[nodiscard]] auto unified_match_result_type(const MatchExpr& me) const -> std::string;

    [[nodiscard]] auto check_try_expr(const TryExpr& tr, int line, int col) -> std::expected<void, std::string>;

    [[nodiscard]] static auto is_likely_interpolation_placeholder(std::string_view sv, const InterpPlaceholder& ph) -> bool;

    [[nodiscard]] auto check_literal_expr(const LiteralExpr& lit, int line, int col = 1) -> std::expected<void, std::string>;

    [[nodiscard]] auto check_unary_expr(const UnaryExpr& un, int line, int col) -> std::expected<void, std::string>;

    [[nodiscard]] auto check_binary_expr(const BinaryExpr& bin, int line, int col) -> std::expected<void, std::string>;

    // "own" / "shared" when `obj` is a smart pointer, "" otherwise.
    [[nodiscard]] auto smart_wrap_of(const ExprPtr& obj) const -> std::string;

    // `own[T].from_raw(p)` — the object is the type expression `own[T]` (parsed as an index).
    [[nodiscard]] static auto own_type_prefix(const ExprPtr& obj) -> std::string;

    // `p.zz` on a struct declared in this program must name a field or a method.
    [[nodiscard]] auto check_known_member(const MemberExpr& mem, int line, int col) -> std::expected<void, std::string>;

    // struct literal fields must exist and have compatible types.
    // `http.ServerLimits` / `h::ServerLimits` -> the stdlib struct row, resolving import aliases.
    [[nodiscard]] auto lookup_stdlib_struct(std::string_view spelled_in) const -> const StdlibStruct*;

    [[nodiscard]] auto check_stdlib_struct_fields(const StdlibStruct& sst, const StructLitExpr& st, int line, int col) -> std::expected<void, std::string>;

    [[nodiscard]] auto check_struct_literal_fields(const StructLitExpr& st, int line, int col) -> std::expected<void, std::string>;

    [[nodiscard]] auto is_move_only_type(TypeId t) const -> bool;

    [[nodiscard]] auto check_move_only_copy(const ExprPtr& rhs, std::string_view target_desc, int line, int col) -> std::expected<void, std::string>;

    // `i32`, `f64`, `bool`, ... used as a value prefix (`i32.MAX`, `f64.parse(s)`).
    [[nodiscard]] auto primitive_type_prefix(const ExprPtr& obj) const -> TypeId;

    [[nodiscard]] auto primitive_bits(TypeId t) const -> std::uint8_t;

    [[nodiscard]] auto check_member_expr(const MemberExpr& mem, int line, int col) -> std::expected<void, std::string>;

    [[nodiscard]] auto check_index_expr(const IndexExpr& idx, int line, int col) -> std::expected<void, std::string>;

    [[nodiscard]] auto check_or_expr(const OrExpr& or_ex, int line, int col, const ExprPtr& self_expr = nullptr) -> std::expected<void, std::string>;

    // parameter types for untyped closure arguments, taken from the callee's `fn(A, B) -> R` parameter
    // (`router.get("/x", |req| ...)` binds `req: http.Request`).
    [[nodiscard]] static auto split_fn_type_params(std::string_view fn_type) -> std::vector<std::string>;

    void record_closure_param_hints(const CallExpr& call);

    // the declared constructor `fn (T) make(...)` of a local type, lowered as `cfire_ctor_T`.
    [[nodiscard]] auto find_user_ctor(const TypeRef& t) const -> const FnDecl*;

    [[nodiscard]] auto find_stdlib_ctor(std::string_view spelled_in) const -> const StdlibCtor*;

    // `make(T[, args])`, `make(own[T][, args])`, `make(shared[T][, args])`, `make([]T, len[, cap])`, `make(map[K]V)`.
    [[nodiscard]] auto check_make_call(const CallExpr& call, int line, int col) -> std::expected<void, std::string>;

    [[nodiscard]] auto check_call_expr(const CallExpr& call, int line, int col, const ExprPtr& self_expr = nullptr) -> std::expected<void, std::string>;

    [[nodiscard]] auto check_match_common(
        const ExprPtr& target,
        bool is_move,
        const std::vector<MatchArm>& arms,
        int line,
        int col,
        bool is_expr_match
    ) -> std::expected<void, std::string>;

    [[nodiscard]] static auto is_slice_exhaustively_covered(const std::vector<MatchArm>& arms) -> bool;

    [[nodiscard]] auto check_match_arms_reachability(
        const std::vector<MatchArm>& arms,
        int match_line,
        int match_col
    ) -> std::expected<void, std::string>;

    [[nodiscard]] static auto stmt_has_break(const StmtPtr& s) -> bool;

    [[nodiscard]] static auto block_has_break(const std::vector<StmtPtr>& body) -> bool;

    [[nodiscard]] static auto stmt_diverges(const StmtPtr& s, const CheckedFile* cf = nullptr) -> bool;

    [[nodiscard]] static auto block_diverges(const std::vector<StmtPtr>& body, const CheckedFile* cf = nullptr) -> bool;

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

    void save_var_before_redecl(const std::string& name);

    void bind_scoped_var(const std::string& name, bool is_mut);

    void bind_match_arm_vars(const MatchArm& arm, const std::string& target_t = "");

    [[nodiscard]] auto check_block_stmts(const std::vector<StmtPtr>& stmts, bool tail_is_value = false) -> std::expected<void, std::string>;

    [[nodiscard]] auto check_stmt(const StmtPtr& s) -> std::expected<void, std::string>;

    [[nodiscard]] auto check_var_decl_stmt(const VarDeclStmt& vd) -> std::expected<void, std::string>;

    [[nodiscard]] auto check_destructure_var_decl_stmt(const DestructureVarDeclStmt& dvs) -> std::expected<void, std::string>;

    [[nodiscard]] auto get_root_variable_name(const ExprPtr& e) const -> std::string;

    // (G1): inside a `scope.spawn` closure, writing through a captured outer pointer (`*p = v`,
    // `p.field = v` via auto-deref) races with every other task holding the same pointer. The one provably
    // disjoint form is allowed: a pointer created inside a `for` body as `&mut xs[i]` from that loop's own
    // index variable, so each task writes a different slot (examples/04-async-workers).
    [[nodiscard]] auto check_spawn_pointer_write(const ExprPtr& target, int line, bool* reported = nullptr) -> std::expected<void, std::string>;

    [[nodiscard]] auto is_concurrency_safe_type(const std::string& ty) const -> bool;

    [[nodiscard]] auto check_assign_stmt(const AssignStmt& as) -> std::expected<void, std::string>;

    [[nodiscard]] auto check_destructure_assign_stmt(const DestructureAssignStmt& das) -> std::expected<void, std::string>;

    [[nodiscard]] auto check_defer_stmt(const DeferStmt& df) -> std::expected<void, std::string>;

    [[nodiscard]] auto check_return_stmt(const ReturnStmt& rt) -> std::expected<void, std::string>;

    [[nodiscard]] auto check_if_stmt(const IfStmt& ifs) -> std::expected<void, std::string>;

    // conditions and logical operands must be `bool`; no implicit truthiness for numbers, strings,
    // optionals or pointers. Only reported when the type is known, so inference gaps never produce errors.
    [[nodiscard]] auto check_bool_condition(const ExprPtr& e, std::string_view what) -> std::expected<void, std::string>;

    [[nodiscard]] auto check_for_stmt(const ForStmt& fs) -> std::expected<void, std::string>;

    [[nodiscard]] auto check_async_scope_stmt(const AsyncScopeStmt& sc) -> std::expected<void, std::string>;

private:
    std::string_view path_;
    std::string pkg_name_{};
    std::string fn_name_{};
    std::string fn_ret_str_{};
    const CheckedFile& cf_;
    std::vector<std::string>& warnings_;
    std::vector<std::string>& errors_;
    std::vector<cfire::diag::StructuredDiagnostic>& diags_;
    bool resilient_{false};
    std::vector<std::string> fallback_errors_{};
    std::vector<cfire::diag::StructuredDiagnostic> fallback_diags_{};
    bool fn_is_fallible_{false};
    bool has_cpp_imports_{false};
    std::unordered_set<std::string> fn_type_params_{};  // the function's and its receiver's type parameters
    std::unordered_map<std::string, std::vector<std::string>> fn_type_param_bounds_{};
    bool seen_cpp_block_{false};
    int scope_depth_{1};
    int in_defer_depth_{0};
    int in_closure_depth_{0};
    int in_escaping_closure_depth_{0};
    int escaping_closure_entry_scope_{0};
    int in_spawn_closure_depth_{0};
    int spawn_entry_scope_{0};
    std::unordered_map<std::string, int> active_loop_index_vars_{};  // name -> nesting count
    std::unordered_set<std::string> disjoint_slot_ptrs_{};
    // the closure being checked and its closure depth, to type its `return` values; the first return
    // type of each closure; and, per scope variable of an active `x := async.scope`, the closures it spawns.
    std::vector<std::pair<const ClosureExpr*, int>> closure_ret_stack_{};
    std::unordered_map<const ClosureExpr*, std::string> closure_ret_types_{};
    std::unordered_map<std::string, std::vector<const ClosureExpr*>> value_scope_spawns_{};
    // slot captures: closures passed to `spawn` (only those may capture a slot), the slices whose slot
    // the task being checked owns (it may write `xs[i]`), and the slices with tasks writing their slots, which the
    // scope body must not resize until the scope ends.
    std::unordered_set<const ClosureExpr*> spawn_closures_{};
    std::unordered_set<std::string> active_slot_captures_{};
    std::unordered_set<std::string> slot_captured_vars_{};

    // `scope.spawn |mut xs[i]| { xs[i] = v }`: each task owns one element. The index is the spawning loop's own index
    // variable (so tasks own different elements), and inside the task `xs` appears only as `xs[i]`.
    [[nodiscard]] auto check_slot_captures(const ClosureExpr& cl, int line) -> std::expected<void, std::string>;
    std::unordered_set<std::string> const_string_vars_{};
    std::unordered_map<const ClosureExpr*, std::vector<std::string>> closure_param_hints_{};
    int loop_depth_{0};
    int in_invalid_unwrap_context_{0};
    std::vector<std::unordered_map<std::string, SavedVarDeclState>*> scope_stack_{};
    std::unordered_set<const Expr*> unwrapped_exprs_{};
    std::unordered_set<const Expr*> discarded_or_exprs_{};
    std::unordered_map<std::string, int> var_scope_depth_{};
    std::unordered_set<std::string> local_defined_vars_{};
    std::unordered_map<std::string, std::string> borrow_origin_{};
    std::unordered_map<std::string, InvalidatedLoan> invalidated_loans_{};
    std::unordered_map<std::string, std::vector<std::string>> extra_borrow_origins_{};
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
    std::unordered_set<const MemberExpr*> callee_members_{};  // member expressions that are call targets
    std::unordered_set<std::string> shared_ptr_var_names_{};  // smart pointers that are `shared[T]`
    std::unordered_set<std::string> foreign_vars_{};
    std::unordered_map<std::string, std::string> var_types_{};
    std::unordered_map<std::string, NullState> ptr_null_state_{};
    std::unordered_map<std::string, std::size_t> fixed_array_lens_{};
    std::unordered_set<std::string> map_vars_{};
    std::unordered_map<std::string, int> moved_at_line_{};
    std::unordered_set<std::string> move_params_{};
    std::unordered_set<std::string> param_names_{};
    std::unordered_set<std::string> mut_vars_{};
    std::unordered_set<std::string> imm_vars_{};
};

#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_SEMA_INFER_IMPL)
#include "type_inference_impl.hpp"
#endif
#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_SEMA_EXPR_IMPL)
#include "expression_checker_impl.hpp"
#endif
#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_SEMA_CALL_IMPL)
#include "call_and_match_checker_impl.hpp"
#endif
#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_SEMA_STMT_IMPL)
#include "statement_checker_impl.hpp"
#endif

} // namespace cfire::frontend
