# Active Workbench: Language Design Audit, Type Conversions & Domain-Distinct I/O (`UPSTREAM-02` & `UPSTREAM-03`)

This document tracks the canonical architecture for:
1. **Domain-Distinct I/O Across Standard Descriptors (`core:os`), Files (`core:fs`), and Sockets (`ext:net`)**
2. **Canonical Constructor Type Conversions (`T(x)`, `[]byte(x)`, `[]u8(x)`, `str(x)`)**
3. **Orthogonal Language Design & Documentation Parity (`CF-001` – `CF-053`)**

---

## 1. Domain-Distinct Input / Output Architecture

### Layer A: Built-in Console Input & Output (Minimal Prelude Primitives)

| Direction | `stdout` Channel | `stderr` Channel |
| :--- | :--- | :--- |
| **Output (Write)** | `print(fmt, ...)`<br/>`println(fmt, ...)` | `eprint(fmt, ...)`<br/>`eprintln(fmt, ...)` |
| **Input (Read Line)** | `readln(prompt = "") -> str?` | `cli.prompt(msg) -> str?`<br/>`cli.prompt_password(msg) -> str?` (in `core:cli`) |

- **`readln(prompt = "") -> str?`**:
  - The single built-in console line reader in the global prelude (`runtime/cfire_prelude.hpp`).
  - If `prompt` is non-empty, writes `prompt` to `stdout` and flushes (`std::fflush(stdout)`).
  - Reads a single line from `stdin` (stripping trailing `\r\n`). Returns `none` on `EOF` (`Ctrl+D` or closed pipe).
- **`cli.prompt(msg = "") -> str?` & `cli.prompt_password(msg = "") -> str?`** (`core:cli`):
  - Prompts on `stderr` and reads from `stdin` (with `termios` `~ECHO` suppression for `prompt_password`), keeping the global prelude clean.

---

### Layer B: Domain-Specific I/O Types (`core:os`, `core:fs`, `ext:net`)

Rather than forcing pipes, files, and sockets into a lowest-common-denominator `os::Stream` wrapper, each domain exposes the exact primitives suited to its semantics while sharing `EINTR`-safe low-level helpers in `cfire::detail`:

| Domain | Type / Module | Core Operations |
| :--- | :--- | :--- |
| **Process FDs & Pipes** | `core:os` (`os.STDIN`, `os.STDOUT`, `os.STDERR`) | `os.fd_read(fd, mut buf) -> usize!`, `os.fd_read_bytes(fd, max) -> []byte!`, `os.fd_write(fd, data) -> usize!`, `os.fd_close(fd) -> void!`, `os.pipe() -> []i32!`, `os.socketpair() -> []i32!`, `os.poll_fd(fd, ms) -> bool`, `os.read_stdin() -> str!`, `os.read_stdin_lines() -> []str!`, `os.read_stdin_bytes(max) -> []byte!` |
| **Filesystem (`FILE*`)** | `core:fs` (`fs::File`) | `fs.open(path, mode) -> File!`, `f.fd() -> i32`, `f.read(mut buf) -> usize!`, `f.read_bytes(max) -> []byte!`, `f.read_line() -> str?`, `f.read_all() -> []byte!`, `f.write(data) -> usize!`, `f.seek(off, whence) -> i64!`, `f.tell() -> i64!`, `f.flush() -> void!`, `f.close()` |
| **Network Sockets** | `ext:net` (`net::TcpConn`) | `net.connect_tcp(host, port) -> TcpConn!`, `c.fd() -> i32`, `c.read(mut buf) -> usize!`, `c.read_bytes(max) -> []byte!`, `c.read_exact(n) -> []byte!`, `c.read_str(max) -> str!`, `c.read_line(max) -> str?`, `c.write(data) -> usize!`, `c.set_timeout_ms(ms)`, `c.close()` |

---

## 2. Canonical Type Conversions (`T(x)`)

1. **Single Canonical Syntax (`T(x)`)**:
   - Scalar conversions: `i8(x)`, `i16(x)`, `i32(x)`, `i64(x)`, `isize(x)`, `u8(x)`, `byte(x)` (returning `std::byte`), `u16(x)`, `u32(x)`, `u64(x)`, `usize(x)`, `f32(x)`, `f64(x)`, `bool(x)` — constrained via `CfireScalarCastable`.
   - Slice & string conversions: `[]byte(s_or_u8_slice)`, `[]u8(s_or_byte_slice)`, and `str(byte_or_u8_slice_or_scalar)`.
   - Writing `x as T` or `: bytes` emits a compile-time diagnostic directing the developer to `T(x)` and `[]byte`.
   - Never emit `dynamic_cast` anywhere in `cfire`.

---

## 3. Judicious Language Design Audit (`CF-001` – `CF-053`)

| Item | Change Audited | Canonical Decision |
| :--- | :--- | :--- |
| **`CF-013`** | `expr as Type` operator | **Removed**: Emit actionable diagnostic pointing to `Type(expr)` or `[]byte(expr)`. |
| **`CF-012`** | `.{field: val}` anonymous struct literal | **Removed**: Keep `TypeName{field: val}` as the single canonical struct literal syntax. |
| **`CF-021`** | `bytes(s)` / `str.from_bytes(b)` / `: bytes` | **Unified**: `[]byte(x)`, `[]u8(x)`, and `str(x)` are the sole canonical slice/string constructors, and `[]byte` is the sole byte-slice type. |
| **`CF-031`** | Top-level `cpp {}` & `@framework`/`@link` | **Removed**: Keep C/C++ interop via `import cpp "..." @pkg(...) @define(...)` and function-local `cpp { ... }`; declare `Frameworks` and `Libraries` on `build.Executable` in `build.cf`. |
| **`CF-018`** | `@allow_fd_growth` / `@no_fd_check` | **Removed**: Pre-warm SQLite's VFS CSPRNG (`sqlite3_randomness`) in test `main()` before `_fd_before` is recorded. |
| **`CF-048`** | `or err { ... }` on `T?` optionals | **Refined**: Support `or { ... }` on both `T?` and `T!`; reject `or err { ... }` on `T?` with `cfire/OPTIONAL-OR-ERR`. |
| **`CF-038`** | Duplicate `stdout_str`/`stdout_cf_` in `os::ExecResult` | **Deduplicated**: Keep single `stdout_cf_` / `stderr_cf_` fields (which `.stdout` and `.stderr` in `.cf` map to). |
