# `cfire` Spec 02 — Clang AST Interop & Boundary Coercion Rules

## 1. Overview
`cfire` guarantees 100% compatibility with C and C++ headers via `import cpp "<header>"`. The compiler invokes `clang++ -std=c++23 -Xclang -ast-dump=json -fsyntax-only` (backed by `.cfire/cache/` AST/PCH caching) to extract C/C++ declarations and perform type-safe boundary coercions.

## 2. Boundary Coercion Table

| Target C/C++ Parameter Type | `cfire` Expression Type | Emitted C++23 Coercion |
| :--- | :--- | :--- |
| `const char*` | `str` (`std::string` or `std::string_view`) | `cfire::c_str(expr)` |
| `std::string_view` | `str` | `std::string_view(expr)` |
| `const std::string&` / `std::string` | `str` (`std::string_view`) | `std::string(expr)` |
| `T*` (mutable pointer) | `&mut val` | `&val` |
| `const T*` | `&val` | `&val` |

## 3. Raw `cplusplus { ... }` Escape Hatch
Any `cplusplus { ... }` block preserves its raw balanced-brace C++23 tokens verbatim inside a scoped block `{ ... }` in the generated `.cpp` file, with all enclosing `cfire` local variables and parameters directly accessible in scope.
