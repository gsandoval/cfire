# `cfire` Spec 01 — Language Frontend & Lowering Rules

## 1. Overview
`cfire` (`.cf`) is a zero-overhead systems programming language that transpiles 1:1 to standard, human-readable **C++23** (`-std=c++23`). Every `.cf` file belongs to a `package` and compiles to `.cfire/gen/<pkg>/<file>.cpp`.

---

## 2. Complete Lexical & EBNF Grammar

```ebnf
SourceFile      = PackageDecl { ImportDecl } { TopLevelDecl } ;
PackageDecl     = "package" Ident ;

ImportDecl      = "import" [ "cpp" ] StringLit [ "as" Ident ] { ImportAttr } ;
ImportAttr      = "@" Ident "(" [ ArgList ] ")" ;

TopLevelDecl    = TypeDecl | FnDecl | VarDecl | TestDecl | CppBlock ;

TypeDecl        = "type" Ident ( StructType | EnumType | InterfaceType | Type ) ;
StructType      = "struct" "{" { FieldDecl } "}" ;
FieldDecl       = Ident ":" Type [ "=" Expr ] [ TagLit ] ;
EnumType        = "enum" "{" { Ident [ "(" TypeList ")" ] [ "," ] } "}" ;
InterfaceType   = "interface" "{" { FnSig } "}" ;

Type            = PrimType | SliceType | ArrayType | MapType | PtrType | SmartPtrType
                | QualifiedIdent [ "[" TypeList "]" ]
                | Type "?" | Type "!" ;

PrimType        = "bool" | "byte" | "i8" | "i16" | "i32" | "i64"
                | "u8" | "u16" | "u32" | "u64" | "isize" | "usize"
                | "f32" | "f64" | "str" | "path" | "bytes" | "void" ;
SliceType       = "[" "]" Type ;
ArrayType       = "[" IntLit "]" Type ;
MapType         = "map" "[" Type "]" Type ;
PtrType         = "*" [ "mut" ] Type ;
SmartPtrType    = ( "own" | "shared" ) "[" Type "]" ;

FnDecl          = "fn" [ Receiver ] Ident [ TypeParams ] "(" [ ParamList ] ")" [ "async" ] [ "->" Type ] Block ;
Receiver        = "(" [ "mut" ] Ident ":" Type ")" ;
ParamList       = Param { "," Param } [ "," ] ;
Param           = [ ParamIntent ] Ident ":" Type [ "=" Expr ] ;
ParamIntent     = "in" | "mut" | "out" | "move" ;

Block           = "{" { Stmt } "}" ;
Stmt            = VarDecl | AssignStmt | IfStmt | ForStmt | MatchStmt
                | DeferStmt | ReturnStmt | CppBlock | ExprStmt ;
```

---

## 3. Surface Reduction 1: Unified `str`, `path`, `bytes`, and Slices

| `cfire` Type | Owning Position (Struct, Local, Return, `move`) | Read-Only `in` Parameter (Default) | Mutable `mut` Parameter |
| :--- | :--- | :--- | :--- |
| **`str`** | `std::string` | `std::string_view` | `std::string&` |
| **`path`** | `std::filesystem::path` | `const std::filesystem::path&` | `std::filesystem::path&` |
| **`bytes`** | `std::vector<std::byte>` | `std::span<const std::byte>` | `std::vector<std::byte>&` |
| **`[]T`** | `std::vector<T>` | `std::span<const T>` | `std::vector<T>&` |

- **String Interpolation:** Double-quoted strings containing `{expr}` lower to `std::format("...", expr)`.
- **Path Joining:** `dir_path / "sub"` uses `std::filesystem::path::operator/`.

---

## 4. Surface Reduction 2: Intent-Based Parameter Semantics

1. **`in` (Default):**
   - Primitive numeric/boolean types (`i32`, `f64`, `bool`, `usize`, etc.) lower to `const T` (or value `T`).
   - `str` lowers to `std::string_view`; `bytes` lowers to `std::span<const std::byte>`; `[]T` lowers to `std::span<const T>`.
   - User `struct` types lower to `const T&`.
2. **`mut`:**
   - Lowers to `T&`.
   - Call-site **must** explicitly annotate `mut arg` (e.g. `record("INFO", mut audit, ...)`). Passing `mut` without the `mut` keyword at the call-site is a semantic error (`cfire/MUT-CALLSITE`).
3. **`out`:**
   - Lowers to `T&`.
   - Call-site **must** explicitly annotate `out arg`.
4. **`move`:**
   - Lowers to `T&&`.
   - Call-site **must** explicitly annotate `move arg`, which emits `std::move(arg)`.
   - Inside the function body, the definite last use of a `move` parameter automatically emits `std::move(param)`. Any subsequent read of a moved variable produces a compile error (`cfire/USE-AFTER-MOVE`).

---

## 5. Surface Reduction 3: Error Handling (`T!`, `try`, `or`) & Coroutines (`async`)

- **`T!`:** Lowers to `std::expected<T, cfire::Error>`.
- **`try expr`:** Unwraps `std::expected` or returns `std::unexpected(res.error())` early from the enclosing function.
- **`expr or err { ... }` / `expr or fallback`:** Evaluates `expr`; on failure, binds `const cfire::Error& err` and executes the recovery block or evaluates `fallback`.
- **`async` & `async.scope`:**
  - `fn foo(...) async -> T` lowers to `cfire::Task<T> foo(...)`.
  - `async.scope(workers: N) |scope| { scope.spawn(...) }` executes tasks across a bounded thread pool and joins all tasks before exiting the lexical scope.
