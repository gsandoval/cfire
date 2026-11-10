#pragma once
// src/ast.hpp — C++23 AST Definitions for cfire (.cf)

#include "lexer.hpp"
#include <atomic>
#include <cstdlib>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

namespace cfire::frontend {

struct Expr;
using ExprPtr = Expr*;

struct Stmt;
using StmtPtr = Stmt*;

// Monotonic Bump Arena Allocator for AST Nodes
class AstArena {
public:
    AstArena() = default;
    ~AstArena() { reset(); }

    AstArena(const AstArena&) = delete;
    AstArena& operator=(const AstArena&) = delete;
    AstArena(AstArena&&) noexcept = default;
    AstArena& operator=(AstArena&&) noexcept = default;

    template <typename T, typename... Args>
    [[nodiscard]] auto alloc(Args&&... args) -> T* {
        void* mem = allocate_bytes(sizeof(T), alignof(T));
        T* obj = ::new (mem) T(std::forward<Args>(args)...);
        if constexpr (!std::is_trivially_destructible_v<T>) {
            destructors_.push_back({
                .fn = [](void* p) { static_cast<T*>(p)->~T(); },
                .obj = obj
            });
        }
        return obj;
    }

    void reset() {
        for (auto it = destructors_.rbegin(); it != destructors_.rend(); ++it) {
            it->fn(it->obj);
        }
        destructors_.clear();
        for (auto& chunk : chunks_) {
            std::free(chunk.data);
        }
        chunks_.clear();
        current_chunk_ = 0;
        current_offset_ = 0;
    }

    [[nodiscard]] auto total_allocated_bytes() const noexcept -> std::size_t {
        std::size_t total = 0;
        for (const auto& c : chunks_) total += c.capacity;
        return total;
    }

private:
    struct Chunk {
        uint8_t* data = nullptr;
        std::size_t capacity = 0;
    };

    struct DestructorNode {
        void (*fn)(void*);
        void* obj;
    };

    auto allocate_bytes(std::size_t size, std::size_t align) -> void* {
        if (current_chunk_ < chunks_.size()) {
            std::size_t aligned_offset = (current_offset_ + (align - 1)) & ~(align - 1);
            if (aligned_offset + size <= chunks_[current_chunk_].capacity) {
                void* ptr = chunks_[current_chunk_].data + aligned_offset;
                current_offset_ = aligned_offset + size;
                return ptr;
            }
        }

        std::size_t new_cap = std::max<std::size_t>(64 * 1024, size + align);
        void* mem = nullptr;
#if !defined(_WIN32)
        if (::posix_memalign(&mem, std::max<std::size_t>(alignof(std::max_align_t), align), new_cap) != 0) {
            mem = std::malloc(new_cap);
        }
#else
        mem = std::malloc(new_cap);
#endif
        uint8_t* new_data = static_cast<uint8_t*>(mem);
        std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(new_data);
        std::uintptr_t aligned_addr = (addr + (align - 1)) & ~static_cast<std::uintptr_t>(align - 1);
        std::size_t aligned_offset = static_cast<std::size_t>(aligned_addr - addr);
        chunks_.push_back(Chunk{.data = new_data, .capacity = new_cap});
        current_chunk_ = chunks_.size() - 1;
        current_offset_ = aligned_offset + size;
        return new_data + aligned_offset;
    }

    std::vector<Chunk> chunks_;
    std::vector<DestructorNode> destructors_;
    std::size_t current_chunk_ = 0;
    std::size_t current_offset_ = 0;
};

inline thread_local AstArena* g_current_arena = nullptr;

struct ArenaScope {
    AstArena* prev = nullptr;
    explicit ArenaScope(AstArena* a) : prev(g_current_arena) { g_current_arena = a; }
    ~ArenaScope() { g_current_arena = prev; }

    ArenaScope(const ArenaScope&) = delete;
    ArenaScope& operator=(const ArenaScope&) = delete;
};

struct TypeRef {
    std::string name{};
    bool is_slice = false;
    bool is_array = false;
    std::string array_len{};
    bool is_map = false;
    std::shared_ptr<TypeRef> key_type = nullptr;
    std::shared_ptr<TypeRef> elem_type = nullptr;
    std::vector<TypeRef> type_args{}; // CF-107: Uniform first-class multi-argument generic type application
    bool is_fallible = false; // T!
    bool is_optional = false; // T?
    bool is_mut_ptr = false;  // *mut T
    bool is_ptr = false;      // *T
    bool is_borrow = false;   // &T (explicit borrowed view)
    bool is_mut_borrow = false; // &mut T
    bool is_tuple = false;    // (T1, T2, ...)
    std::vector<TypeRef> tuple_types{};
    bool is_fn = false;       // fn(T1, T2) -> R
    std::vector<TypeRef> fn_params{};
    std::shared_ptr<TypeRef> fn_ret = nullptr;
    std::string smart_wrap{}; // "own" or "shared"
};

struct ImportAttr {
    std::string name{};
    std::vector<std::string> args{};
};

struct ImportDecl {
    bool is_cpp = false;
    std::string path{};
    std::string alias{};
    std::vector<ImportAttr> attrs{};
    int line = 1;
};

struct StructField {
    std::string name{};
    TypeRef type{};
    ExprPtr default_val = nullptr;
    std::string raw_tag{};
    std::unordered_map<std::string, std::string> tags{};
    int line = 1;

    [[nodiscard]] auto get_tag(const std::string& key) const -> std::optional<std::string> {
        if (auto it = tags.find(key); it != tags.end()) return it->second;
        return std::nullopt;
    }
};

[[nodiscard]] inline auto parse_struct_tags(std::string_view raw_tag)
    -> std::vector<std::pair<std::string, std::string>> {
    std::vector<std::pair<std::string, std::string>> out;
    std::size_t i = 0;
    const std::size_t n = raw_tag.size();
    auto is_word = [](char c) noexcept {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    };
    while (i < n) {
        while (i < n && !is_word(raw_tag[i])) ++i;
        if (i >= n) break;
        std::size_t k_start = i;
        while (i < n && is_word(raw_tag[i])) ++i;
        std::string_view key = raw_tag.substr(k_start, i - k_start);
        if (i + 1 < n && raw_tag[i] == ':' && raw_tag[i + 1] == '"') {
            i += 2;
            std::size_t v_start = i;
            while (i < n && raw_tag[i] != '"') ++i;
            if (i < n && raw_tag[i] == '"') {
                std::string_view val = raw_tag.substr(v_start, i - v_start);
                out.emplace_back(std::string(key), std::string(val));
                ++i;
            }
        }
    }
    return out;
}

struct StructDecl {
    std::string name{};
    std::vector<StructField> fields{};
    int line = 1;
    std::string source_path{};
    std::string package_name{};
};

struct EnumVariant {
    std::string name{};
    std::vector<TypeRef> payload_types{};
    int line = 1;
};

struct EnumDecl {
    std::string name{};
    std::vector<EnumVariant> variants{};
    int line = 1;
    std::string source_path{};
    std::string package_name{};
};

struct InterfaceMethodSig {
    std::string name{};
    std::vector<TypeRef> param_types{};
    TypeRef return_type{"void"};
};

struct InterfaceDecl {
    std::string name{};
    std::vector<InterfaceMethodSig> methods{};
    int line = 1;
};

struct TypeParam {
    std::string name{};
    std::string constraint{}; // optional interface/concept name `T: Hasher`
};

struct ReceiverDecl {
    std::string intent = "in"; // "in" or "mut"
    std::string name{};
    TypeRef type{};
    bool has_explicit_in = false;
    bool has_deprecated_mut_pos = false;
    int line = 1;
    int col = 1;
};

struct ParamDecl {
    std::string intent = "in"; // "in", "mut", "out", "move"
    std::string name{};
    TypeRef type{};
    int line = 1;
    int col = 1;
    bool has_explicit_in = false;
    bool has_deprecated_mut_pos = false;
};

// --- Semantic Resolution & Symbol Kinds (CF-115) ---

enum class SymbolKind {
    Local,
    Param,
    Global,
    Function,
    Struct,
    EnumVariant,
    Field,
    Method,
    Package,
    CoreNamespace
};

struct Resolution {
    SymbolKind kind = SymbolKind::Local;
    std::string canonical_name{};   // e.g. "cfire::os::stdin_file", "cfire_pkg_foo__bar", "cfire_method_baz"
    std::string type_name{};        // inferred or declared type name
    std::string owner_type{};       // for Field, Method, EnumVariant (e.g. "Point", "Shape")
    std::string package_name{};     // for package-scoped symbols
    bool is_mut = false;
    bool is_optional = false;
    bool is_smart_ptr = false;
    bool is_raw_ptr = false;
    bool is_auto_deref = false;     // true if access requires auto-deref (*obj)
};

[[nodiscard]] inline auto sanitize_cpp_ident(std::string_view id) -> std::string {
    static const std::unordered_set<std::string_view> kCppReserved = {
        "short", "long", "signed", "unsigned", "class", "template", "operator",
        "export", "delete", "new", "private", "protected", "public", "virtual",
        "override", "final", "typename", "namespace", "using", "this", "throw",
        "catch", "union", "goto", "register", "volatile", "friend", "explicit",
        "mutable", "inline", "constexpr", "consteval", "constinit", "co_await",
        "co_return", "co_yield", "concept", "requires", "stdout", "stderr", "stdin"
    };
    if (kCppReserved.contains(id)) {
        return std::string(id) + "_cf_";
    }
    return std::string(id);
}

// --- Expressions ---

struct IdentExpr {
    std::string name{};
    std::optional<Resolution> resolution{};
};

struct ExprSnippetResult {
    std::shared_ptr<AstArena> arena;
    ExprPtr expr = nullptr;
};

struct LiteralExpr {
    TokenKind kind = TokenKind::Int;
    std::string value{};
    mutable std::vector<ExprSnippetResult> interp_snippets{};
};

struct UnaryExpr {
    std::string op{}; // "!", "*", "&", "&mut"
    ExprPtr operand{};
};

struct BinaryExpr {
    ExprPtr left{};
    std::string op{};
    ExprPtr right{};
};

struct MemberExpr {
    ExprPtr object{};
    std::string field{};
    bool is_namespace = false; // `::`
    std::optional<Resolution> resolution{};
};

struct IndexExpr {
    ExprPtr object{};
    ExprPtr index{};
};

struct SliceExpr {
    ExprPtr object{};
    ExprPtr start = nullptr; // nil means 0
    ExprPtr end = nullptr;   // nil means .size()
};

struct StructLitField {
    std::string name{};
    ExprPtr value{};
};

struct StructLitExpr {
    std::string type_name{};
    std::vector<TypeRef> type_args{};
    std::vector<StructLitField> fields{};
};

struct ArrayLitExpr {
    std::vector<ExprPtr> elements{};
};

struct ClosureExpr {
    std::vector<std::string> params{};
    std::vector<std::string> param_intents{};
    std::vector<std::optional<TypeRef>> param_types{};
    std::optional<TypeRef> return_type{};
    bool is_async = false;
    bool is_escaping = false;
    std::vector<StmtPtr> body{};
};

struct CallArg {
    std::string intent{}; // "", "mut", "out", "move"
    std::string label{};  // optional named arg `level: 3`
    ExprPtr value{};
    int line = 1;
    int col = 1;
};

struct CallExpr {
    ExprPtr callee{};
    std::vector<TypeRef> type_args{};
    std::vector<CallArg> args{};
    std::optional<ClosureExpr> block_arg{};
    std::optional<Resolution> resolution{};
};

struct TryExpr {
    ExprPtr inner{};
};

struct OrExpr {
    ExprPtr inner{};
    std::string err_var{};
    std::vector<StmtPtr> handler_body{};
    ExprPtr fallback_val = nullptr;
};

struct TupleExpr {
    std::vector<ExprPtr> elements{};
};

struct MatchArm {
    std::string enum_prefix{}; // optional `Shape::`
    std::string variant_name{};
    std::vector<std::string> bindings{};
    std::string literal_val{}; // for primitive/string literals e.g. "\"GET\""

    // Slice pattern matching:
    bool is_slice_pattern = false;
    std::vector<std::string> slice_head{};
    std::string slice_rest{}; // e.g. "tail" in "..tail", or empty if no rest or ".._"
    bool slice_has_rest = false;
    std::vector<std::string> slice_tail{};

    std::vector<StmtPtr> body{};
    int line = 1;
};

struct MatchExpr {
    ExprPtr target{};
    bool is_move = false;
    std::vector<MatchArm> arms{};
    int line = 1;
    int col = 1;
};

using ExprVariant = std::variant<
    IdentExpr,
    LiteralExpr,
    UnaryExpr,
    BinaryExpr,
    MemberExpr,
    IndexExpr,
    SliceExpr,
    StructLitExpr,
    ArrayLitExpr,
    ClosureExpr,
    CallExpr,
    TryExpr,
    OrExpr,
    TupleExpr,
    MatchExpr>;

inline std::atomic<uint64_t> g_expr_id_seq{1};

struct Expr {
    ExprVariant node;
    int line = 1;
    int col = 1;
    uint64_t id = 0;

    Expr() = default;
    Expr(ExprVariant n, int l, int c, uint64_t i = 0) : node(std::move(n)), line(l), col(c), id(i) {}
};

template <typename T>
[[nodiscard]] inline auto make_expr(T&& v, int line, int col) -> ExprPtr {
    uint64_t nid = g_expr_id_seq.fetch_add(1, std::memory_order_relaxed);
    if (g_current_arena) {
        return g_current_arena->alloc<Expr>(ExprVariant{std::forward<T>(v)}, line, col, nid);
    }
    static thread_local AstArena fallback_arena;
    return fallback_arena.alloc<Expr>(ExprVariant{std::forward<T>(v)}, line, col, nid);
}

// --- Statements ---

struct VarDeclStmt {
    std::string name{};
    bool is_mut = false;
    std::optional<TypeRef> type{};
    ExprPtr init = nullptr;
    bool was_var = false;
    int line = 1;
    int col = 1;
};

struct AssignStmt {
    ExprPtr target{};
    std::string op{}; // "=" or "+="
    ExprPtr value{};
    int line = 1;
};

struct DeferStmt {
    ExprPtr call{};
    int line = 1;
    std::vector<StmtPtr> body{};
};

struct ReturnStmt {
    ExprPtr value = nullptr;
    int line = 1;
};

struct BreakStmt {
    int line = 1;
};

struct ContinueStmt {
    int line = 1;
};

struct IfStmt {
    ExprPtr cond{};
    std::vector<StmtPtr> then_body{};
    std::vector<StmtPtr> else_body{};
    int line = 1;
};

struct ForStmt {
    std::string index_var{};
    std::string item_var{};
    ExprPtr iter_expr{};
    std::vector<StmtPtr> body{};
    int line = 1;
    ExprPtr range_end = nullptr;
    bool range_inclusive = false;
};

struct MatchStmt {
    ExprPtr target{};
    bool is_move = false;
    std::vector<MatchArm> arms{};
    int line = 1;
    int col = 1;
};

struct CppBlockStmt {
    std::string raw_code{};
    int line = 1;
};

struct AsyncScopeStmt {
    ExprPtr workers{};
    std::string scope_var{};
    std::vector<StmtPtr> body{};
    int line = 1;
};

struct ExprStmt {
    ExprPtr expr{};
    int line = 1;
};

struct DestructureVarDeclStmt {
    std::vector<std::pair<std::string, bool>> bindings{}; // (name, is_mut)
    ExprPtr init = nullptr;
    int line = 1;
    int col = 1;
};

struct DestructureAssignStmt {
    std::vector<ExprPtr> targets{};
    std::string op{"="};
    ExprPtr value = nullptr;
    int line = 1;
};

using StmtVariant = std::variant<
    VarDeclStmt,
    AssignStmt,
    DeferStmt,
    ReturnStmt,
    BreakStmt,
    ContinueStmt,
    IfStmt,
    ForStmt,
    MatchStmt,
    CppBlockStmt,
    AsyncScopeStmt,
    ExprStmt,
    DestructureVarDeclStmt,
    DestructureAssignStmt>;

struct Stmt {
    StmtVariant node;
    int line = 1;

    Stmt() = default;
    Stmt(StmtVariant n, int l) : node(std::move(n)), line(l) {}
};

template <typename T>
[[nodiscard]] inline auto make_stmt(T&& v, int line) -> StmtPtr {
    if (g_current_arena) {
        return g_current_arena->alloc<Stmt>(StmtVariant{std::forward<T>(v)}, line);
    }
    static thread_local AstArena fallback_arena;
    return fallback_arena.alloc<Stmt>(StmtVariant{std::forward<T>(v)}, line);
}

// --- Top-Level Declarations ---

struct FnDecl {
    std::optional<ReceiverDecl> receiver{};
    std::string name{};
    std::vector<TypeParam> type_params{};
    std::vector<ParamDecl> params{};
    bool is_async = false;
    TypeRef return_type{"void"};
    std::vector<StmtPtr> body{};
    int line = 1;
    std::string package_name{};
    std::string source_path{};
};

struct GlobalVarDecl {
    std::string name{};
    bool is_mut = false;
    std::optional<TypeRef> type{};
    ExprPtr value{};
    bool was_var = false;
    int line = 1;
    std::string package_name{};
    std::string source_path{};
};

struct BenchParamVariation {
    std::string name{};
    TypeRef inferred_type{"i64"};
    std::vector<ExprPtr> values{};
    int line = 1;
};

struct TestDecl {
    std::string name{};
    std::vector<StmtPtr> body{};
    int line = 1;
    bool is_fuzz = false;
    bool is_bench = false;
    std::vector<ParamDecl> params{};
    std::vector<BenchParamVariation> bench_variations{};
    std::string source_path{};
};

struct FileAST {
    std::string path{};
    std::string package_name{};
    std::vector<ImportDecl> imports{};
    std::vector<StructDecl> structs{};
    std::vector<EnumDecl> enums{};
    std::vector<InterfaceDecl> interfaces{};
    std::vector<FnDecl> functions{};
    std::vector<GlobalVarDecl> global_vars{};
    std::vector<TestDecl> tests{};
    std::shared_ptr<AstArena> arena = nullptr;
    std::vector<std::shared_ptr<AstArena>> child_arenas{};
};

} // namespace cfire::frontend
