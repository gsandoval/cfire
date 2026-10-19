#pragma once
// src/ast.hpp — C++23 AST Definitions for cfire (.cf)

#include "lexer.hpp"
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace cfire::frontend {

struct Expr;
using ExprPtr = std::shared_ptr<Expr>;

struct Stmt;
using StmtPtr = std::shared_ptr<Stmt>;

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
    TupleExpr>;

struct Expr {
    ExprVariant node;
    int line = 1;
    int col = 1;
};

template <typename T>
[[nodiscard]] inline auto make_expr(T&& v, int line, int col) -> ExprPtr {
    return std::make_shared<Expr>(Expr{ExprVariant{std::forward<T>(v)}, line, col});
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

struct MatchArm {
    std::string enum_prefix{}; // optional `Shape::`
    std::string variant_name{};
    std::vector<std::string> bindings{};

    // Slice pattern matching:
    bool is_slice_pattern = false;
    std::vector<std::string> slice_head{};
    std::string slice_rest{}; // e.g. "tail" in "..tail", or empty if no rest or ".._"
    bool slice_has_rest = false;
    std::vector<std::string> slice_tail{};

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
    return std::make_shared<Stmt>(Stmt{StmtVariant{std::forward<T>(v)}, line});
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
};

} // namespace cfire::frontend
