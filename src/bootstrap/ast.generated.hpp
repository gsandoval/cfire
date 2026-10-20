#include "cfire_prelude.hpp"

struct TypeRef;
struct ImportAttr;
struct ImportDecl;
struct StructField;
struct StructDecl;
struct EnumVariant;
struct EnumDecl;
struct InterfaceMethodSig;
struct InterfaceDecl;
struct TypeParam;
struct ReceiverDecl;
struct ParamDecl;
struct IdentExpr;
struct LiteralExpr;
struct UnaryExpr;
struct BinaryExpr;
struct MemberExpr;
struct IndexExpr;
struct SliceExpr;
struct StructLitField;
struct StructLitExpr;
struct ArrayLitExpr;
struct ClosureExpr;
struct CallArg;
struct CallExpr;
struct TryExpr;
struct OrExpr;
struct TupleExpr;
struct Expr;
struct VarDeclStmt;
struct AssignStmt;
struct DeferStmt;
struct ReturnStmt;
struct IfStmt;
struct ForStmt;
struct MatchArm;
struct MatchStmt;
struct CppBlockStmt;
struct AsyncScopeStmt;
struct ExprStmt;
struct BindingPair;
struct DestructureVarDeclStmt;
struct DestructureAssignStmt;
struct Stmt;
struct FnDecl;
struct GlobalVarDecl;
struct TestDecl;
struct FileAST;

struct TypeRef {
    std::string name{};
    bool is_slice{};
    bool is_array{};
    std::string array_len{};
    bool is_map{};
    std::shared_ptr<TypeRef> key_type{};
    std::shared_ptr<TypeRef> elem_type{};
    bool is_fallible{};
    bool is_optional{};
    bool is_mut_ptr{};
    bool is_ptr{};
    bool is_borrow{};
    bool is_mut_borrow{};
    bool is_tuple{};
    std::vector<TypeRef> tuple_types{};
    std::string smart_wrap{};
};

template <>
struct cfire::reflect_struct<TypeRef> {
    static constexpr std::string_view name = "TypeRef";
    static constexpr std::size_t field_count = 16;
    static constexpr std::array<std::string_view, 16> field_names = {
        "name",
        "is_slice",
        "is_array",
        "array_len",
        "is_map",
        "key_type",
        "elem_type",
        "is_fallible",
        "is_optional",
        "is_mut_ptr",
        "is_ptr",
        "is_borrow",
        "is_mut_borrow",
        "is_tuple",
        "tuple_types",
        "smart_wrap",
    };
    static constexpr std::array<std::string_view, 16> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct ImportAttr {
    std::string name{};
    std::vector<std::string> args{};
};

template <>
struct cfire::reflect_struct<ImportAttr> {
    static constexpr std::string_view name = "ImportAttr";
    static constexpr std::size_t field_count = 2;
    static constexpr std::array<std::string_view, 2> field_names = {
        "name",
        "args",
    };
    static constexpr std::array<std::string_view, 2> field_tags = {
        R"tag()tag",
        R"tag()tag",
    };
};

struct ImportDecl {
    bool is_cpp{};
    std::string path{};
    std::string alias{};
    std::vector<ImportAttr> attrs{};
    std::int32_t line{};
};

template <>
struct cfire::reflect_struct<ImportDecl> {
    static constexpr std::string_view name = "ImportDecl";
    static constexpr std::size_t field_count = 5;
    static constexpr std::array<std::string_view, 5> field_names = {
        "is_cpp",
        "path",
        "alias",
        "attrs",
        "line",
    };
    static constexpr std::array<std::string_view, 5> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct StructField {
    std::string name{};
    TypeRef field_type{};
    std::shared_ptr<Expr> default_val{};
    std::string raw_tag{};
    std::int32_t line{};
};

template <>
struct cfire::reflect_struct<StructField> {
    static constexpr std::string_view name = "StructField";
    static constexpr std::size_t field_count = 5;
    static constexpr std::array<std::string_view, 5> field_names = {
        "name",
        "field_type",
        "default_val",
        "raw_tag",
        "line",
    };
    static constexpr std::array<std::string_view, 5> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct StructDecl {
    std::string name{};
    std::vector<StructField> fields{};
    std::int32_t line{};
};

template <>
struct cfire::reflect_struct<StructDecl> {
    static constexpr std::string_view name = "StructDecl";
    static constexpr std::size_t field_count = 3;
    static constexpr std::array<std::string_view, 3> field_names = {
        "name",
        "fields",
        "line",
    };
    static constexpr std::array<std::string_view, 3> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct EnumVariant {
    std::string name{};
    std::vector<TypeRef> payload_types{};
    std::int32_t line{};
};

template <>
struct cfire::reflect_struct<EnumVariant> {
    static constexpr std::string_view name = "EnumVariant";
    static constexpr std::size_t field_count = 3;
    static constexpr std::array<std::string_view, 3> field_names = {
        "name",
        "payload_types",
        "line",
    };
    static constexpr std::array<std::string_view, 3> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct EnumDecl {
    std::string name{};
    std::vector<EnumVariant> variants{};
    std::int32_t line{};
};

template <>
struct cfire::reflect_struct<EnumDecl> {
    static constexpr std::string_view name = "EnumDecl";
    static constexpr std::size_t field_count = 3;
    static constexpr std::array<std::string_view, 3> field_names = {
        "name",
        "variants",
        "line",
    };
    static constexpr std::array<std::string_view, 3> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct InterfaceMethodSig {
    std::string name{};
    std::vector<TypeRef> param_types{};
    TypeRef return_type{};
};

template <>
struct cfire::reflect_struct<InterfaceMethodSig> {
    static constexpr std::string_view name = "InterfaceMethodSig";
    static constexpr std::size_t field_count = 3;
    static constexpr std::array<std::string_view, 3> field_names = {
        "name",
        "param_types",
        "return_type",
    };
    static constexpr std::array<std::string_view, 3> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct InterfaceDecl {
    std::string name{};
    std::vector<InterfaceMethodSig> methods{};
    std::int32_t line{};
};

template <>
struct cfire::reflect_struct<InterfaceDecl> {
    static constexpr std::string_view name = "InterfaceDecl";
    static constexpr std::size_t field_count = 3;
    static constexpr std::array<std::string_view, 3> field_names = {
        "name",
        "methods",
        "line",
    };
    static constexpr std::array<std::string_view, 3> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct TypeParam {
    std::string name{};
    std::string constraint{};
};

template <>
struct cfire::reflect_struct<TypeParam> {
    static constexpr std::string_view name = "TypeParam";
    static constexpr std::size_t field_count = 2;
    static constexpr std::array<std::string_view, 2> field_names = {
        "name",
        "constraint",
    };
    static constexpr std::array<std::string_view, 2> field_tags = {
        R"tag()tag",
        R"tag()tag",
    };
};

struct ReceiverDecl {
    std::string intent{};
    std::string name{};
    TypeRef receiver_type{};
};

template <>
struct cfire::reflect_struct<ReceiverDecl> {
    static constexpr std::string_view name = "ReceiverDecl";
    static constexpr std::size_t field_count = 3;
    static constexpr std::array<std::string_view, 3> field_names = {
        "intent",
        "name",
        "receiver_type",
    };
    static constexpr std::array<std::string_view, 3> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct ParamDecl {
    std::string intent{};
    std::string name{};
    TypeRef param_type{};
    std::int32_t line{};
    std::int32_t col{};
};

template <>
struct cfire::reflect_struct<ParamDecl> {
    static constexpr std::string_view name = "ParamDecl";
    static constexpr std::size_t field_count = 5;
    static constexpr std::array<std::string_view, 5> field_names = {
        "intent",
        "name",
        "param_type",
        "line",
        "col",
    };
    static constexpr std::array<std::string_view, 5> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct IdentExpr {
    std::string name{};
};

template <>
struct cfire::reflect_struct<IdentExpr> {
    static constexpr std::string_view name = "IdentExpr";
    static constexpr std::size_t field_count = 1;
    static constexpr std::array<std::string_view, 1> field_names = {
        "name",
    };
    static constexpr std::array<std::string_view, 1> field_tags = {
        R"tag()tag",
    };
};

struct LiteralExpr {
    std::int32_t kind{};
    std::string value{};
};

template <>
struct cfire::reflect_struct<LiteralExpr> {
    static constexpr std::string_view name = "LiteralExpr";
    static constexpr std::size_t field_count = 2;
    static constexpr std::array<std::string_view, 2> field_names = {
        "kind",
        "value",
    };
    static constexpr std::array<std::string_view, 2> field_tags = {
        R"tag()tag",
        R"tag()tag",
    };
};

struct UnaryExpr {
    std::string op{};
    std::shared_ptr<Expr> operand{};
};

template <>
struct cfire::reflect_struct<UnaryExpr> {
    static constexpr std::string_view name = "UnaryExpr";
    static constexpr std::size_t field_count = 2;
    static constexpr std::array<std::string_view, 2> field_names = {
        "op",
        "operand",
    };
    static constexpr std::array<std::string_view, 2> field_tags = {
        R"tag()tag",
        R"tag()tag",
    };
};

struct BinaryExpr {
    std::shared_ptr<Expr> left{};
    std::string op{};
    std::shared_ptr<Expr> right{};
};

template <>
struct cfire::reflect_struct<BinaryExpr> {
    static constexpr std::string_view name = "BinaryExpr";
    static constexpr std::size_t field_count = 3;
    static constexpr std::array<std::string_view, 3> field_names = {
        "left",
        "op",
        "right",
    };
    static constexpr std::array<std::string_view, 3> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct MemberExpr {
    std::shared_ptr<Expr> object{};
    std::string field{};
    bool is_namespace{};
};

template <>
struct cfire::reflect_struct<MemberExpr> {
    static constexpr std::string_view name = "MemberExpr";
    static constexpr std::size_t field_count = 3;
    static constexpr std::array<std::string_view, 3> field_names = {
        "object",
        "field",
        "is_namespace",
    };
    static constexpr std::array<std::string_view, 3> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct IndexExpr {
    std::shared_ptr<Expr> object{};
    std::shared_ptr<Expr> index{};
};

template <>
struct cfire::reflect_struct<IndexExpr> {
    static constexpr std::string_view name = "IndexExpr";
    static constexpr std::size_t field_count = 2;
    static constexpr std::array<std::string_view, 2> field_names = {
        "object",
        "index",
    };
    static constexpr std::array<std::string_view, 2> field_tags = {
        R"tag()tag",
        R"tag()tag",
    };
};

struct SliceExpr {
    std::shared_ptr<Expr> object{};
    std::shared_ptr<Expr> start{};
    std::shared_ptr<Expr> end{};
};

template <>
struct cfire::reflect_struct<SliceExpr> {
    static constexpr std::string_view name = "SliceExpr";
    static constexpr std::size_t field_count = 3;
    static constexpr std::array<std::string_view, 3> field_names = {
        "object",
        "start",
        "end",
    };
    static constexpr std::array<std::string_view, 3> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct StructLitField {
    std::string name{};
    std::shared_ptr<Expr> value{};
};

template <>
struct cfire::reflect_struct<StructLitField> {
    static constexpr std::string_view name = "StructLitField";
    static constexpr std::size_t field_count = 2;
    static constexpr std::array<std::string_view, 2> field_names = {
        "name",
        "value",
    };
    static constexpr std::array<std::string_view, 2> field_tags = {
        R"tag()tag",
        R"tag()tag",
    };
};

struct StructLitExpr {
    std::string type_name{};
    std::vector<StructLitField> fields{};
};

template <>
struct cfire::reflect_struct<StructLitExpr> {
    static constexpr std::string_view name = "StructLitExpr";
    static constexpr std::size_t field_count = 2;
    static constexpr std::array<std::string_view, 2> field_names = {
        "type_name",
        "fields",
    };
    static constexpr std::array<std::string_view, 2> field_tags = {
        R"tag()tag",
        R"tag()tag",
    };
};

struct ArrayLitExpr {
    std::vector<std::shared_ptr<Expr>> elements{};
};

template <>
struct cfire::reflect_struct<ArrayLitExpr> {
    static constexpr std::string_view name = "ArrayLitExpr";
    static constexpr std::size_t field_count = 1;
    static constexpr std::array<std::string_view, 1> field_names = {
        "elements",
    };
    static constexpr std::array<std::string_view, 1> field_tags = {
        R"tag()tag",
    };
};

struct ClosureExpr {
    std::vector<std::string> params{};
    bool is_async{};
    std::vector<std::shared_ptr<Stmt>> body{};
};

template <>
struct cfire::reflect_struct<ClosureExpr> {
    static constexpr std::string_view name = "ClosureExpr";
    static constexpr std::size_t field_count = 3;
    static constexpr std::array<std::string_view, 3> field_names = {
        "params",
        "is_async",
        "body",
    };
    static constexpr std::array<std::string_view, 3> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct CallArg {
    std::string intent{};
    std::string label{};
    std::shared_ptr<Expr> value{};
    std::int32_t line{};
    std::int32_t col{};
};

template <>
struct cfire::reflect_struct<CallArg> {
    static constexpr std::string_view name = "CallArg";
    static constexpr std::size_t field_count = 5;
    static constexpr std::array<std::string_view, 5> field_names = {
        "intent",
        "label",
        "value",
        "line",
        "col",
    };
    static constexpr std::array<std::string_view, 5> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct CallExpr {
    std::shared_ptr<Expr> callee{};
    std::vector<TypeRef> type_args{};
    std::vector<CallArg> args{};
    std::shared_ptr<ClosureExpr> block_arg{};
};

template <>
struct cfire::reflect_struct<CallExpr> {
    static constexpr std::string_view name = "CallExpr";
    static constexpr std::size_t field_count = 4;
    static constexpr std::array<std::string_view, 4> field_names = {
        "callee",
        "type_args",
        "args",
        "block_arg",
    };
    static constexpr std::array<std::string_view, 4> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct TryExpr {
    std::shared_ptr<Expr> inner{};
};

template <>
struct cfire::reflect_struct<TryExpr> {
    static constexpr std::string_view name = "TryExpr";
    static constexpr std::size_t field_count = 1;
    static constexpr std::array<std::string_view, 1> field_names = {
        "inner",
    };
    static constexpr std::array<std::string_view, 1> field_tags = {
        R"tag()tag",
    };
};

struct OrExpr {
    std::shared_ptr<Expr> inner{};
    std::string err_var{};
    std::vector<std::shared_ptr<Stmt>> handler_body{};
    std::shared_ptr<Expr> fallback_val{};
};

template <>
struct cfire::reflect_struct<OrExpr> {
    static constexpr std::string_view name = "OrExpr";
    static constexpr std::size_t field_count = 4;
    static constexpr std::array<std::string_view, 4> field_names = {
        "inner",
        "err_var",
        "handler_body",
        "fallback_val",
    };
    static constexpr std::array<std::string_view, 4> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct TupleExpr {
    std::vector<std::shared_ptr<Expr>> elements{};
};

template <>
struct cfire::reflect_struct<TupleExpr> {
    static constexpr std::string_view name = "TupleExpr";
    static constexpr std::size_t field_count = 1;
    static constexpr std::array<std::string_view, 1> field_names = {
        "elements",
    };
    static constexpr std::array<std::string_view, 1> field_tags = {
        R"tag()tag",
    };
};

struct ExprNode {
    struct _Ident {
        IdentExpr _0{};
    };
    struct _Literal {
        LiteralExpr _0{};
    };
    struct _Unary {
        UnaryExpr _0{};
    };
    struct _Binary {
        BinaryExpr _0{};
    };
    struct _Member {
        MemberExpr _0{};
    };
    struct _Index {
        IndexExpr _0{};
    };
    struct _Slice {
        SliceExpr _0{};
    };
    struct _StructLit {
        StructLitExpr _0{};
    };
    struct _ArrayLit {
        ArrayLitExpr _0{};
    };
    struct _Closure {
        ClosureExpr _0{};
    };
    struct _Call {
        CallExpr _0{};
    };
    struct _Try {
        TryExpr _0{};
    };
    struct _Or {
        OrExpr _0{};
    };
    struct _Tuple {
        TupleExpr _0{};
    };
    std::variant<_Ident, _Literal, _Unary, _Binary, _Member, _Index, _Slice, _StructLit, _ArrayLit, _Closure, _Call, _Try, _Or, _Tuple> _v;
    [[nodiscard]] static auto Ident(IdentExpr a0) -> ExprNode { return ExprNode{_Ident{std::move(a0)}}; }
    [[nodiscard]] static auto Literal(LiteralExpr a0) -> ExprNode { return ExprNode{_Literal{std::move(a0)}}; }
    [[nodiscard]] static auto Unary(UnaryExpr a0) -> ExprNode { return ExprNode{_Unary{std::move(a0)}}; }
    [[nodiscard]] static auto Binary(BinaryExpr a0) -> ExprNode { return ExprNode{_Binary{std::move(a0)}}; }
    [[nodiscard]] static auto Member(MemberExpr a0) -> ExprNode { return ExprNode{_Member{std::move(a0)}}; }
    [[nodiscard]] static auto Index(IndexExpr a0) -> ExprNode { return ExprNode{_Index{std::move(a0)}}; }
    [[nodiscard]] static auto Slice(SliceExpr a0) -> ExprNode { return ExprNode{_Slice{std::move(a0)}}; }
    [[nodiscard]] static auto StructLit(StructLitExpr a0) -> ExprNode { return ExprNode{_StructLit{std::move(a0)}}; }
    [[nodiscard]] static auto ArrayLit(ArrayLitExpr a0) -> ExprNode { return ExprNode{_ArrayLit{std::move(a0)}}; }
    [[nodiscard]] static auto Closure(ClosureExpr a0) -> ExprNode { return ExprNode{_Closure{std::move(a0)}}; }
    [[nodiscard]] static auto Call(CallExpr a0) -> ExprNode { return ExprNode{_Call{std::move(a0)}}; }
    [[nodiscard]] static auto Try(TryExpr a0) -> ExprNode { return ExprNode{_Try{std::move(a0)}}; }
    [[nodiscard]] static auto Or(OrExpr a0) -> ExprNode { return ExprNode{_Or{std::move(a0)}}; }
    [[nodiscard]] static auto Tuple(TupleExpr a0) -> ExprNode { return ExprNode{_Tuple{std::move(a0)}}; }
};

struct Expr {
    ExprNode node{};
    std::int32_t line{};
    std::int32_t col{};
};

template <>
struct cfire::reflect_struct<Expr> {
    static constexpr std::string_view name = "Expr";
    static constexpr std::size_t field_count = 3;
    static constexpr std::array<std::string_view, 3> field_names = {
        "node",
        "line",
        "col",
    };
    static constexpr std::array<std::string_view, 3> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct VarDeclStmt {
    std::string name{};
    bool is_mut{};
    TypeRef type_annot{};
    bool has_type{};
    std::shared_ptr<Expr> init{};
    bool was_var{};
    std::int32_t line{};
    std::int32_t col{};
};

template <>
struct cfire::reflect_struct<VarDeclStmt> {
    static constexpr std::string_view name = "VarDeclStmt";
    static constexpr std::size_t field_count = 8;
    static constexpr std::array<std::string_view, 8> field_names = {
        "name",
        "is_mut",
        "type_annot",
        "has_type",
        "init",
        "was_var",
        "line",
        "col",
    };
    static constexpr std::array<std::string_view, 8> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct AssignStmt {
    std::shared_ptr<Expr> target{};
    std::string op{};
    std::shared_ptr<Expr> value{};
    std::int32_t line{};
};

template <>
struct cfire::reflect_struct<AssignStmt> {
    static constexpr std::string_view name = "AssignStmt";
    static constexpr std::size_t field_count = 4;
    static constexpr std::array<std::string_view, 4> field_names = {
        "target",
        "op",
        "value",
        "line",
    };
    static constexpr std::array<std::string_view, 4> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct DeferStmt {
    std::shared_ptr<Expr> call{};
    std::int32_t line{};
};

template <>
struct cfire::reflect_struct<DeferStmt> {
    static constexpr std::string_view name = "DeferStmt";
    static constexpr std::size_t field_count = 2;
    static constexpr std::array<std::string_view, 2> field_names = {
        "call",
        "line",
    };
    static constexpr std::array<std::string_view, 2> field_tags = {
        R"tag()tag",
        R"tag()tag",
    };
};

struct ReturnStmt {
    std::shared_ptr<Expr> value{};
    std::int32_t line{};
};

template <>
struct cfire::reflect_struct<ReturnStmt> {
    static constexpr std::string_view name = "ReturnStmt";
    static constexpr std::size_t field_count = 2;
    static constexpr std::array<std::string_view, 2> field_names = {
        "value",
        "line",
    };
    static constexpr std::array<std::string_view, 2> field_tags = {
        R"tag()tag",
        R"tag()tag",
    };
};

struct IfStmt {
    std::shared_ptr<Expr> cond{};
    std::vector<std::shared_ptr<Stmt>> then_body{};
    std::vector<std::shared_ptr<Stmt>> else_body{};
    std::int32_t line{};
};

template <>
struct cfire::reflect_struct<IfStmt> {
    static constexpr std::string_view name = "IfStmt";
    static constexpr std::size_t field_count = 4;
    static constexpr std::array<std::string_view, 4> field_names = {
        "cond",
        "then_body",
        "else_body",
        "line",
    };
    static constexpr std::array<std::string_view, 4> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct ForStmt {
    std::string index_var{};
    std::string item_var{};
    std::shared_ptr<Expr> iter_expr{};
    std::vector<std::shared_ptr<Stmt>> body{};
    std::int32_t line{};
};

template <>
struct cfire::reflect_struct<ForStmt> {
    static constexpr std::string_view name = "ForStmt";
    static constexpr std::size_t field_count = 5;
    static constexpr std::array<std::string_view, 5> field_names = {
        "index_var",
        "item_var",
        "iter_expr",
        "body",
        "line",
    };
    static constexpr std::array<std::string_view, 5> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct MatchArm {
    std::string enum_prefix{};
    std::string variant_name{};
    std::vector<std::string> bindings{};
    bool is_slice_pattern{};
    std::vector<std::string> slice_head{};
    std::string slice_rest{};
    bool slice_has_rest{};
    std::vector<std::string> slice_tail{};
    std::vector<std::shared_ptr<Stmt>> body{};
    std::int32_t line{};
};

template <>
struct cfire::reflect_struct<MatchArm> {
    static constexpr std::string_view name = "MatchArm";
    static constexpr std::size_t field_count = 10;
    static constexpr std::array<std::string_view, 10> field_names = {
        "enum_prefix",
        "variant_name",
        "bindings",
        "is_slice_pattern",
        "slice_head",
        "slice_rest",
        "slice_has_rest",
        "slice_tail",
        "body",
        "line",
    };
    static constexpr std::array<std::string_view, 10> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct MatchStmt {
    std::shared_ptr<Expr> target{};
    bool is_move{};
    std::vector<MatchArm> arms{};
    std::int32_t line{};
    std::int32_t col{};
};

template <>
struct cfire::reflect_struct<MatchStmt> {
    static constexpr std::string_view name = "MatchStmt";
    static constexpr std::size_t field_count = 5;
    static constexpr std::array<std::string_view, 5> field_names = {
        "target",
        "is_move",
        "arms",
        "line",
        "col",
    };
    static constexpr std::array<std::string_view, 5> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct CppBlockStmt {
    std::string raw_code{};
    std::int32_t line{};
};

template <>
struct cfire::reflect_struct<CppBlockStmt> {
    static constexpr std::string_view name = "CppBlockStmt";
    static constexpr std::size_t field_count = 2;
    static constexpr std::array<std::string_view, 2> field_names = {
        "raw_code",
        "line",
    };
    static constexpr std::array<std::string_view, 2> field_tags = {
        R"tag()tag",
        R"tag()tag",
    };
};

struct AsyncScopeStmt {
    std::shared_ptr<Expr> workers{};
    std::string scope_var{};
    std::vector<std::shared_ptr<Stmt>> body{};
    std::int32_t line{};
};

template <>
struct cfire::reflect_struct<AsyncScopeStmt> {
    static constexpr std::string_view name = "AsyncScopeStmt";
    static constexpr std::size_t field_count = 4;
    static constexpr std::array<std::string_view, 4> field_names = {
        "workers",
        "scope_var",
        "body",
        "line",
    };
    static constexpr std::array<std::string_view, 4> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct ExprStmt {
    std::shared_ptr<Expr> expr{};
    std::int32_t line{};
};

template <>
struct cfire::reflect_struct<ExprStmt> {
    static constexpr std::string_view name = "ExprStmt";
    static constexpr std::size_t field_count = 2;
    static constexpr std::array<std::string_view, 2> field_names = {
        "expr",
        "line",
    };
    static constexpr std::array<std::string_view, 2> field_tags = {
        R"tag()tag",
        R"tag()tag",
    };
};

struct BindingPair {
    std::string name{};
    bool is_mut{};
};

template <>
struct cfire::reflect_struct<BindingPair> {
    static constexpr std::string_view name = "BindingPair";
    static constexpr std::size_t field_count = 2;
    static constexpr std::array<std::string_view, 2> field_names = {
        "name",
        "is_mut",
    };
    static constexpr std::array<std::string_view, 2> field_tags = {
        R"tag()tag",
        R"tag()tag",
    };
};

struct DestructureVarDeclStmt {
    std::vector<BindingPair> bindings{};
    std::shared_ptr<Expr> init{};
    std::int32_t line{};
    std::int32_t col{};
};

template <>
struct cfire::reflect_struct<DestructureVarDeclStmt> {
    static constexpr std::string_view name = "DestructureVarDeclStmt";
    static constexpr std::size_t field_count = 4;
    static constexpr std::array<std::string_view, 4> field_names = {
        "bindings",
        "init",
        "line",
        "col",
    };
    static constexpr std::array<std::string_view, 4> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct DestructureAssignStmt {
    std::vector<std::shared_ptr<Expr>> targets{};
    std::string op{};
    std::shared_ptr<Expr> value{};
    std::int32_t line{};
};

template <>
struct cfire::reflect_struct<DestructureAssignStmt> {
    static constexpr std::string_view name = "DestructureAssignStmt";
    static constexpr std::size_t field_count = 4;
    static constexpr std::array<std::string_view, 4> field_names = {
        "targets",
        "op",
        "value",
        "line",
    };
    static constexpr std::array<std::string_view, 4> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct StmtNode {
    struct _VarDecl {
        VarDeclStmt _0{};
    };
    struct _Assign {
        AssignStmt _0{};
    };
    struct _Defer {
        DeferStmt _0{};
    };
    struct _Return {
        ReturnStmt _0{};
    };
    struct _If {
        IfStmt _0{};
    };
    struct _For {
        ForStmt _0{};
    };
    struct _Match {
        MatchStmt _0{};
    };
    struct _CppBlock {
        CppBlockStmt _0{};
    };
    struct _AsyncScope {
        AsyncScopeStmt _0{};
    };
    struct _Expr {
        ExprStmt _0{};
    };
    struct _DestructureVarDecl {
        DestructureVarDeclStmt _0{};
    };
    struct _DestructureAssign {
        DestructureAssignStmt _0{};
    };
    std::variant<_VarDecl, _Assign, _Defer, _Return, _If, _For, _Match, _CppBlock, _AsyncScope, _Expr, _DestructureVarDecl, _DestructureAssign> _v;
    [[nodiscard]] static auto VarDecl(VarDeclStmt a0) -> StmtNode { return StmtNode{_VarDecl{std::move(a0)}}; }
    [[nodiscard]] static auto Assign(AssignStmt a0) -> StmtNode { return StmtNode{_Assign{std::move(a0)}}; }
    [[nodiscard]] static auto Defer(DeferStmt a0) -> StmtNode { return StmtNode{_Defer{std::move(a0)}}; }
    [[nodiscard]] static auto Return(ReturnStmt a0) -> StmtNode { return StmtNode{_Return{std::move(a0)}}; }
    [[nodiscard]] static auto If(IfStmt a0) -> StmtNode { return StmtNode{_If{std::move(a0)}}; }
    [[nodiscard]] static auto For(ForStmt a0) -> StmtNode { return StmtNode{_For{std::move(a0)}}; }
    [[nodiscard]] static auto Match(MatchStmt a0) -> StmtNode { return StmtNode{_Match{std::move(a0)}}; }
    [[nodiscard]] static auto CppBlock(CppBlockStmt a0) -> StmtNode { return StmtNode{_CppBlock{std::move(a0)}}; }
    [[nodiscard]] static auto AsyncScope(AsyncScopeStmt a0) -> StmtNode { return StmtNode{_AsyncScope{std::move(a0)}}; }
    [[nodiscard]] static auto Expr(ExprStmt a0) -> StmtNode { return StmtNode{_Expr{std::move(a0)}}; }
    [[nodiscard]] static auto DestructureVarDecl(DestructureVarDeclStmt a0) -> StmtNode { return StmtNode{_DestructureVarDecl{std::move(a0)}}; }
    [[nodiscard]] static auto DestructureAssign(DestructureAssignStmt a0) -> StmtNode { return StmtNode{_DestructureAssign{std::move(a0)}}; }
};

struct Stmt {
    StmtNode node{};
    std::int32_t line{};
};

template <>
struct cfire::reflect_struct<Stmt> {
    static constexpr std::string_view name = "Stmt";
    static constexpr std::size_t field_count = 2;
    static constexpr std::array<std::string_view, 2> field_names = {
        "node",
        "line",
    };
    static constexpr std::array<std::string_view, 2> field_tags = {
        R"tag()tag",
        R"tag()tag",
    };
};

struct FnDecl {
    bool has_receiver{};
    ReceiverDecl receiver{};
    std::string name{};
    std::vector<TypeParam> type_params{};
    std::vector<ParamDecl> params{};
    bool is_async{};
    TypeRef return_type{};
    std::vector<std::shared_ptr<Stmt>> body{};
    std::int32_t line{};
};

template <>
struct cfire::reflect_struct<FnDecl> {
    static constexpr std::string_view name = "FnDecl";
    static constexpr std::size_t field_count = 9;
    static constexpr std::array<std::string_view, 9> field_names = {
        "has_receiver",
        "receiver",
        "name",
        "type_params",
        "params",
        "is_async",
        "return_type",
        "body",
        "line",
    };
    static constexpr std::array<std::string_view, 9> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct GlobalVarDecl {
    std::string name{};
    bool is_mut{};
    bool has_type{};
    TypeRef type_annot{};
    std::shared_ptr<Expr> value{};
    bool was_var{};
    std::int32_t line{};
};

template <>
struct cfire::reflect_struct<GlobalVarDecl> {
    static constexpr std::string_view name = "GlobalVarDecl";
    static constexpr std::size_t field_count = 7;
    static constexpr std::array<std::string_view, 7> field_names = {
        "name",
        "is_mut",
        "has_type",
        "type_annot",
        "value",
        "was_var",
        "line",
    };
    static constexpr std::array<std::string_view, 7> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

struct TestDecl {
    std::string name{};
    std::vector<std::shared_ptr<Stmt>> body{};
    std::int32_t line{};
};

template <>
struct cfire::reflect_struct<TestDecl> {
    static constexpr std::string_view name = "TestDecl";
    static constexpr std::size_t field_count = 3;
    static constexpr std::array<std::string_view, 3> field_names = {
        "name",
        "body",
        "line",
    };
    static constexpr std::array<std::string_view, 3> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
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

template <>
struct cfire::reflect_struct<FileAST> {
    static constexpr std::string_view name = "FileAST";
    static constexpr std::size_t field_count = 9;
    static constexpr std::array<std::string_view, 9> field_names = {
        "path",
        "package_name",
        "imports",
        "structs",
        "enums",
        "interfaces",
        "functions",
        "global_vars",
        "tests",
    };
    static constexpr std::array<std::string_view, 9> field_tags = {
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
        R"tag()tag",
    };
};

[[nodiscard]] auto make_typeref(std::string_view name) -> TypeRef {
    return TypeRef{.name = ("" + name), .is_slice = false, .is_array = false, .array_len = "", .is_map = false, .is_fallible = false, .is_optional = false, .is_mut_ptr = false, .is_ptr = false, .is_borrow = false, .is_mut_borrow = false, .is_tuple = false, .tuple_types = {}, .smart_wrap = ""};
}

[[nodiscard]] auto format_typeref_cf(const TypeRef& t) -> std::string {
    std::string base = "";
    if (t.is_tuple) {
        base = "(";
        std::size_t i = 0;
        auto n = t.tuple_types.size();
        while (i < n) {
            if (i > 0) {
                base = (base + ", ");
            }
            base = (base + format_typeref_cf(t.tuple_types[i]));
            i = (i + 1);
        }
        base = (base + ")");
    } else {
        if ((t.smart_wrap.size() > 0) && (t.elem_type != std::nullopt)) {
            base = (((t.smart_wrap + "[") + format_typeref_cf((*t.elem_type))) + "]");
        } else {
            if (t.is_slice && (t.elem_type != std::nullopt)) {
                base = ("[]" + format_typeref_cf((*t.elem_type)));
            } else {
                if (t.is_array && (t.elem_type != std::nullopt)) {
                    base = ((("[" + t.array_len) + "]") + format_typeref_cf((*t.elem_type)));
                } else {
                    if ((t.is_map && (t.key_type != std::nullopt)) && (t.elem_type != std::nullopt)) {
                        base = ((("map[" + format_typeref_cf((*t.key_type))) + "]") + format_typeref_cf((*t.elem_type)));
                    } else {
                        if (t.is_mut_ptr && (t.elem_type != std::nullopt)) {
                            base = ("*mut " + format_typeref_cf((*t.elem_type)));
                        } else {
                            if (t.is_ptr && (t.elem_type != std::nullopt)) {
                                base = ("*" + format_typeref_cf((*t.elem_type)));
                            } else {
                                if (t.is_mut_borrow && (t.elem_type != std::nullopt)) {
                                    base = ("&mut " + format_typeref_cf((*t.elem_type)));
                                } else {
                                    if (t.is_borrow && (t.elem_type != std::nullopt)) {
                                        base = ("&" + format_typeref_cf((*t.elem_type)));
                                    } else {
                                        if (t.name.size() == 0) {
                                            base = "void";
                                        } else {
                                            base = t.name;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    if (t.is_optional) {
        base = (base + "?");
    }
    if (t.is_fallible) {
        base = (base + "!");
    }
    return base;
}

[[nodiscard]] auto count_ast_declarations(const FileAST& ast) -> std::tuple<std::size_t, std::size_t, std::size_t> {
    return std::make_tuple(ast.structs.size(), ast.enums.size(), ast.functions.size());
}
