# Project Backlog

This backlog tracks platform milestones for `cfire` across the [`cfire-upstream.md`](./cfire-upstream.md) (`CF-001` – `CF-053`) resolution milestone. All 53 items are now resolved.

---

## Completed Platform Milestones (`CF-001`, `CF-005`, `CF-010`, `CF-052`)

| ID | Priority | Component | Summary | Status |
| :--- | :---: | :--- | :--- | :---: |
| `PLAT-P1-01` (`CF-001`) | P1 | `runtime/core/webui/windows_webview2.hpp`, `src/buildsys.hpp` | **Windows Desktop Backend (`Win32` + `WebView2`)**: Native Windows window lifecycle (`CreateWindowExW` + `WM_USER + 0xCF`) and `ICoreWebView2` `cfire://app/` virtual host + IPC handler | `[DONE]` |
| `PLAT-P2-01` (`CF-005`) | P2 | `runtime/core/ssh.hpp`, `src/buildsys.hpp` | **First-Class `ext:net/ssh` Client Module (`libssh`)**: SSH dial with timeout, SHA-256 host-key fingerprints, key-by-content auth (`auth_publickey_mem`), PTY shell (`open_pty`, `resize_pty`), `exec`, SFTP, and port forwarding | `[DONE]` |
| `PLAT-P2-02` (`CF-010`) | P2 | `runtime/cfire_prelude.hpp`, `runtime/core/os.hpp`, `runtime/core/keychain.hpp`, `runtime/core/sqlite.hpp` | **Native Windows (`_WIN32`) Runtime Support**: `CreateProcessA` subprocesses, `_pipe`, `GetConsoleScreenBufferInfo`, `SetConsoleMode` raw terminal & no-echo password prompt, `WSAPoll`, Windows Credential Manager (`CredWriteA`/`CredReadA`), and `.string().c_str()` SQLite path fix | `[DONE]` |
| `PLAT-P2-03` (`CF-052`) | P2 | `src/buildsys.hpp`, `runtime/core/webui.hpp` | **Custom URL Scheme Registration (`CFBundleURLTypes`), `.svg` Icon Conversion, `app.set_icon`, & Code Signing (`SignIdentity`)**: Extended `build.cf` with `UrlSchemes`, `SignIdentity`, `.svg` icon conversion, and runtime window icon loading | `[DONE]` |
