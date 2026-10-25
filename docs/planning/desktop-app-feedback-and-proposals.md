# Real-World Systems App Feedback & Language Proposal

> **Source**: Empirical feedback collected from building the **Flautas CMS Desktop** native GUI application using Cfire with Raylib/Raygui bindings.
> **Date**: October 2026
> **Objective**: Eliminate recurring `cpp {}` escape hatches in Cfire GUI and systems applications by extending the language, prelude, and standard library.

---

## Executive Summary

While building the native desktop GUI for Flautas CMS (a multi-pane, high-DPI desktop editor with custom text engine, UTF-8 Spanish accent popovers, live preview server, and splitters), 38 separate `cpp {}` blocks were originally needed. 

An audit of these blocks revealed that Cfire's core type system, borrow checker, and build system are remarkably solid. However, developers are forced into C++ escape hatches primarily by **5 specific ergonomic gaps**:

1. **Lack of Built-in Primitive Numeric Cast Operators / Prelude Constructors** (`i32(v)`, `f32(v)`).
2. **Lack of Pointer Offsets and C FFI decay for `str` / slices** (`.c_str() + offset`, `&int_out_param`).
3. **Lack of Bitwise Operators on Imported C / Vendor Enums** (`FLAG_A | FLAG_B`).
4. **Missing Reverse Find and Index Primitives in `core:str`** (`str.rfind`, `str.last_index_of`, `str.index_of`).
5. **Lack of Rebindable Pointer / Reference Aliases** (`*mut T` or `ref mut`) when routing actions dynamically between multiple string buffers.

Addressing these items will enable 100% pure Cfire code in native GUI, audio, game, and system applications without resorting to inline C++.

---

## Detailed Proposals & Backlog Items

### `LANG-P0-01`: Built-in Numeric Conversion / Typecast Operators & Prelude Constructors
- **Priority**: P0
- **Category**: Compiler & Prelude (`cfire_prelude.hpp`, `src/codegen.hpp`, `src/sema.hpp`)
- **Problem Statement**:
  Writing `i32(my_float)` or `f32(my_int)` in `.cf` source currently passes through to generated C++ as `i32(my_float)` verbatim. Because `cfire_prelude.hpp` defines `using i32 = int32_t;` (a type alias, not a callable function), Clang rejects this with:
  `error: use of undeclared identifier 'i32'`
  Developers are forced to either define custom helpers like `to_i32(v: f32) -> i32` or drop into `cpp { static_cast<int>(v); }`.
- **Proposed Solution**:
  1. In `cfire_prelude.hpp`, define inline conversion function templates for all primitive types:
     ```cpp
     template <typename T> constexpr int32_t i32(T v) noexcept { return static_cast<int32_t>(v); }
     template <typename T> constexpr int64_t i64(T v) noexcept { return static_cast<int64_t>(v); }
     template <typename T> constexpr uint32_t u32(T v) noexcept { return static_cast<uint32_t>(v); }
     template <typename T> constexpr uint64_t u64(T v) noexcept { return static_cast<uint64_t>(v); }
     template <typename T> constexpr float f32(T v) noexcept { return static_cast<float>(v); }
     template <typename T> constexpr double f64(T v) noexcept { return static_cast<double>(v); }
     ```
  2. Optionally support `expr as T` or `cast(expr, T)` syntax in `src/parser.hpp` and `src/codegen.hpp`.
- **Acceptance Criteria**:
  - `i32(3.14f)` and `f32(42)` compile cleanly in `.cf` code with zero warnings and zero escape hatches.

---

### `LANG-P1-02`: Pointer Offsets, Decays, and C FFI Interop for `str` & Slices
- **Priority**: P1
- **Category**: Standard Library & FFI (`runtime/core/str.hpp`, `src/sema.hpp`)
- **Problem Statement**:
  When interfacing with C libraries (like Raylib's `GetCodepointNext(const char* text, int* codepointSize)`), developers need:
  1. Pointer arithmetic into strings (e.g. `s.c_str() + byte_offset`).
  2. Passing addresses of local variables as C out-parameters (`&bytes_read`).
- **Proposed Solution**:
  1. Expose `str.byte_ptr(s: str) -> *u8` and `str.byte_ptr_at(s: str, offset: i32) -> *u8`.
  2. Allow taking the address of a local variable for C FFI bindings: `&mut my_int`.
- **Acceptance Criteria**:
  - Calling C FFI functions that take pointer offsets into strings or out-parameter pointers does not require `cpp {}`.

---

### `LANG-P1-03`: Bitwise Operator Support on Imported Vendor / C Enums
- **Priority**: P1
- **Category**: Compiler & Clang Bridge (`src/clang_bridge.hpp`, `src/codegen.hpp`)
- **Problem Statement**:
  Vendor libraries frequently use enums as bitflags (e.g., `rl.FLAG_WINDOW_HIGHDPI | rl.FLAG_WINDOW_RESIZABLE`). In Cfire, applying `|` or `&` on vendor enum types triggers a type mismatch error because the bridge registers them as distinct enum types without bitwise operator overloads.
- **Proposed Solution**:
  1. When emitting headers or types for imported vendor C enums, emit bitwise operator overloads:
     ```cpp
     inline constexpr ConfigFlags operator|(ConfigFlags a, ConfigFlags b) noexcept {
         return static_cast<ConfigFlags>(static_cast<unsigned int>(a) | static_cast<unsigned int>(b));
     }
     inline constexpr ConfigFlags operator&(ConfigFlags a, ConfigFlags b) noexcept {
         return static_cast<ConfigFlags>(static_cast<unsigned int>(a) & static_cast<unsigned int>(b));
     }
     ```
- **Acceptance Criteria**:
  - Enum flags can be combined using `|` directly in `.cf` source without manual integer casting.

---

### `STDLIB-P1-04`: Reverse Find & Substring Index Primitives in `core:str`
- **Priority**: P1
- **Category**: Standard Library (`runtime/core/str.hpp`, `runtime/cfire_core_stdlib.hpp`)
- **Problem Statement**:
  `core:str` currently provides `starts_with`, `ends_with`, `contains`, `replace`, and `split`, but lacks:
  - `str.index_of(s: str, substr: str) -> i32` (-1 if not found)
  - `str.last_index_of(s: str, substr: str) -> i32` (-1 if not found)
  - `str.rfind(s: str, substr: str) -> i32`
  This forced the Flautas desktop editor to drop into C++ `std::string::rfind` for markdown heading and image detection.
- **Proposed Solution**:
  Implement `index_of`, `last_index_of`, and `rfind` in `runtime/core/str.hpp` utilizing `std::string_view::find` and `std::string_view::rfind`.
- **Acceptance Criteria**:
  - `str.last_index_of("/path/to/file.md", "/")` returns `11`.
  - Comprehensive unit tests added to `tests/unit/test_stdlib.cpp`.

---

### `LANG-P2-05`: Rebindable Pointer / Reference Aliases (`*mut T` or `ref mut`)
- **Priority**: P2
- **Category**: Language Design & SEMA (`src/ast.hpp`, `src/sema.hpp`, `src/codegen.hpp`)
- **Problem Statement**:
  Interactive text editors and UI property inspectors often operate on one of many string buffers (18 in Flautas CMS: title, body, slug, author, categories, field definitions, etc.).
  In C++, developers simply rebind a pointer:
  ```cpp
  std::string* target = &state.edit_body;
  if (state.active_field == 1) target = &state.edit_title;
  // Generic editing engine modifies *target
  ```
  In pure Cfire, variables cannot be re-bound to point to different mutable fields of a struct. To avoid duplicating 600 lines of cursor, selection, clipboard, undo/redo, and UTF-8 navigation logic for each field, the developer had to use C++.
- **Proposed Solution**:
  Support rebindable pointer/reference variables, e.g.:
  ```cfire
  mut target: *mut str = &mut state.edit_body
  if state.active_field == 1 {
      target = &mut state.edit_title
  }
  *target = *target + "!"
  ```
- **Acceptance Criteria**:
  - Rebindable mutable pointers/references allow multi-buffer editors to share a single implementation without code duplication.

---

### `LANG-P1-06`: Slice and Vector `.data()` Buffer Pointer Access for C FFI Interop
- **Priority**: P1
- **Category**: Language Design & Codegen (`src/codegen.hpp`, `src/sema.hpp`, `docs/reference.html`)
- **Problem Statement**:
  While Cfire provided string byte pointer access via `.byte_ptr()` / `bytes_str()` (lowering to `cfire::str::byte_ptr(s)`), slices and vectors (`[]T`, `std::vector<T>`, `std::span<T>`) cannot directly yield their raw element buffer pointer (`*const T` or `*mut T`) in pure `.cf` code.
  When interfacing with C libraries that expect contiguous buffer arrays (such as Raylib's `LoadFontEx(fileName: str, fontSize: i32, codepoints: *i32, codepointCount: i32)`), developers cannot pass `codepoints.data()`.
  This forced lingering `cpp {}` blocks in font loaders and binary buffer integrations simply to write `codepoints.data()`.
- **Proposed Solution**:
  1. Add built-in member method `.data()` (and `.data_ptr()`) for slice/vector expressions in `src/codegen.hpp`:
     ```cpp
     if ((mem->field == "data" || mem->field == "data_ptr") && call.args.empty()) {
         return std::format("({}).data()", emit_expr(mem->object));
     }
     ```
  2. Ensure SEMA permits `.data()` on slice and vector types, returning a raw element pointer `*T` (or `*const T` for immutable receivers).
  3. Complement existing `byte_ptr()` on `str` so that:
     - `my_str.byte_ptr()` yields `*const c_char`
     - `my_slice.data()` yields `*const T` (or `*mut T` on mutable slices)
- **Acceptance Criteria**:
  - `mut codepoints: []i32 = [32, 33, 34]`
  - `f := rl.LoadFontEx("font.ttf", 48, codepoints.data(), codepoints.len())` compiles and links without `cpp {}`.
  - Added unit test in `tests/e2e/test_canonical.cpp` and `tests/unit/test_stdlib.cpp`.
