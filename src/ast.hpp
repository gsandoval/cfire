#pragma once
// src/ast.hpp — C++23 AST Definitions for cfire (.cf)

#include "lexer.hpp"
#include <cstdlib>
#include <memory>
#include <new>
#include <optional>
#include <string>
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
        if (::posix_memalign(&mem, std::max<std::size_t>(alignof(std::max_align_t), align), new_cap) != 0) {
            mem = std::malloc(new_cap);
        }
        uint8_t* new_data = static_cast<uint8_t*>(mem);
        chunks_.push_back(Chunk{.data = new_data, .capacity = new_cap});
        current_chunk_ = chunks_.size() - 1;
        std::size_t aligned_offset = (0 + (align - 1)) & ~(align - 1);
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
    bool is_fallible = false; // T!
    bool is_optional = false; // T?
    bool is_mut_ptr = false;  // *mut T
    bool is_ptr = false;      // *T
    bool is_borrow = false;   // &T (explicit borrowed view)
    bool is_mut_borrow = false; // &mut T
    bool is_tuple = false;    // (T1, T2, ...)
    std::vector<TypeRef> tuple_types{};
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

struct StructDecl {
    std::string name{};
    std::vector<StructField> fields{};
    int line = 1;
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
};

struct ParamDecl {
    std::string intent = "in"; // "in", "mut", "out", "move"
    std::string name{};
    TypeRef type{};
    int line = 1;
    int col = 1;
};

// --- Expressions ---

struct IdentExpr {
    std::string name{};
};

struct LiteralExpr {
    TokenKind kind = TokenKind::Int;
    std::string value{};
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
    std::vector<StructLitField> fields{};
};

struct ArrayLitExpr {
    std::vector<ExprPtr> elements{};
};

struct ClosureExpr {
    std::vector<std::string> params{};
    bool is_async = false;
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

struct Expr {
    ExprVariant node;
    int line = 1;
    int col = 1;
};

template <typename T>
[[nodiscard]] inline auto make_expr(T&& v, int line, int col) -> ExprPtr {
    if (g_current_arena) {
        return g_current_arena->alloc<Expr>(ExprVariant{std::forward<T>(v)}, line, col);
    }
    static thread_local AstArena fallback_arena;
    return fallback_arena.alloc<Expr>(ExprVariant{std::forward<T>(v)}, line, col);
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
};

struct ReturnStmt {
    ExprPtr value = nullptr;
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
};

struct GlobalVarDecl {
    std::string name{};
    bool is_mut = false;
    std::optional<TypeRef> type{};
    ExprPtr value{};
    bool was_var = false;
    int line = 1;
};

struct TestDecl {
    std::string name{};
    std::vector<StmtPtr> body{};
    int line = 1;
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
