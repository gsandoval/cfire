// tools/vmtest/main.cpp — runs the CI-equivalent gate inside a local UTM VM (C++ UTM gate runner).
//   .vmtest linux | windows | both        run the gate(s) on the current working tree (uncommitted changes included)
//   .vmtest setup-linux | setup-windows   install the toolchain in the guest once (apt packages / MSYS2 CLANG64)
// Settings come from CFIRE_VM_* environment variables (tools/vmtest/vmtest.hpp). See scripts/vm/README.md.
//
// `utmctl exec` only launches the job: its own output capture can return before a guest command finishes, so every
// job uploads its log and exit code to the file server here (`PUT /results/<job>.*`) and this driver waits for them.

#include "../../tests/process_run.hpp"
#include "vmtest.hpp"

#if defined(_WIN32)
auto main() -> int {
    std::println(stderr, "vmtest drives UTM, which runs on macOS; there is nothing to do on Windows");
    return 2;
}
#else
#include <algorithm>
#include <arpa/inet.h>
#include <mutex>
#include <netinet/in.h>
#include <ranges>
#include <sys/socket.h>
#include <unordered_set>

namespace {

namespace fs = std::filesystem;
using namespace std::chrono_literals;

auto die(std::string_view msg) -> int {
    std::println(stderr, "error: {}", msg);
    return 1;
}

// One run per mode at a time: a second run would overwrite the share copy and the guest job under it. The lock is a
// directory holding the owner's PID, released on every exit path; a lock whose owner is gone is taken over.
class RunLock {
public:
    RunLock(fs::path dir) : dir_(std::move(dir)) {}
    auto acquire() -> bool {
        std::error_code ec;
        if (fs::create_directory(dir_, ec)) return own();
        if (auto pid_text = cfire::fs::read_text(dir_ / "pid")) {
            const int pid = std::atoi(pid_text->c_str());
            if (pid > 0 && ::kill(pid, 0) != 0 && errno == ESRCH) {
                std::println("==> taking over a stale lock left by pid {}", pid);
                return own();
            }
        }
        return false;
    }
    ~RunLock() {
        if (!held_) return;
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    RunLock(const RunLock&) = delete;
    auto operator=(const RunLock&) -> RunLock& = delete;

private:
    auto own() -> bool {
        held_ = cfire::fs::write_text(dir_ / "pid", std::to_string(::getpid())).has_value();
        return held_;
    }
    fs::path dir_;
    bool held_ = false;
};

// A minimal HTTP/1.1 file server on the host-only address: GET/HEAD of share files, PUT under results/.
class FileServer {
public:
    FileServer(fs::path root) : root_(std::move(root)) {}
    ~FileServer() { stop(); }
    FileServer(const FileServer&) = delete;
    auto operator=(const FileServer&) -> FileServer& = delete;

    // false with `in_use` set when another server already holds the port.
    auto start(const std::string& ip, int port, bool& in_use) -> bool {
        in_use = false;
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return false;
        int one = 1;
        ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<std::uint16_t>(port));
        if (::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) return false;
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            in_use = errno == EADDRINUSE;
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        if (::listen(fd_, 16) != 0) return false;
        thread_ = std::jthread([this](std::stop_token st) { serve(st); });
        return true;
    }
    void stop() {
        if (fd_ >= 0) {
            ::shutdown(fd_, SHUT_RDWR);
            ::close(fd_);
            fd_ = -1;
        }
        if (thread_.joinable()) {
            thread_.request_stop();
            thread_.join();
        }
    }

private:
    void serve(std::stop_token st) {
        while (!st.stop_requested()) {
            const int c = ::accept(fd_, nullptr, nullptr);
            if (c < 0) {
                if (st.stop_requested() || fd_ < 0) return;
                continue;
            }
            std::jthread([this, c] { handle(c); }).detach();
        }
    }
    static auto send_all(int c, std::string_view data) -> bool {
        while (!data.empty()) {
            const auto n = ::send(c, data.data(), data.size(), 0);
            if (n <= 0) return false;
            data.remove_prefix(static_cast<std::size_t>(n));
        }
        return true;
    }
    static void reply(int c, int code, std::string_view reason, std::string_view body = {}, bool head_only = false) {
        (void)send_all(c, std::format("HTTP/1.1 {} {}\r\nContent-Length: {}\r\nConnection: close\r\n\r\n", code, reason, body.size()));
        if (!head_only) (void)send_all(c, body);
    }
    void handle(int c) {
        timeval tv{30, 0};
        ::setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        std::string req;
        char buf[65536];
        std::size_t header_end = std::string::npos;
        while ((header_end = req.find("\r\n\r\n")) == std::string::npos && req.size() < 65536) {
            const auto n = ::recv(c, buf, sizeof(buf), 0);
            if (n <= 0) break;
            req.append(buf, static_cast<std::size_t>(n));
        }
        if (header_end == std::string::npos) {
            ::close(c);
            return;
        }
        const auto line_end = req.find("\r\n");
        const auto sp1 = req.find(' ');
        const auto sp2 = req.find(' ', sp1 + 1);
        const std::string method = req.substr(0, sp1);
        const std::string url = sp1 < line_end && sp2 < line_end ? req.substr(sp1 + 1, sp2 - sp1 - 1) : "";
        const auto target = vmtest::resolve_request(root_, method, url);
        if (!target) {
            reply(c, 403, "Forbidden");
        } else if (method == "PUT") {
            std::size_t length = 0;
            std::string headers = req.substr(0, header_end);
            std::ranges::transform(headers, headers.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (auto p = headers.find("\r\ncontent-length:"); p != std::string::npos) length = std::strtoull(headers.c_str() + p + 17, nullptr, 10);
            std::string body = req.substr(header_end + 4);
            while (body.size() < length) {
                const auto n = ::recv(c, buf, sizeof(buf), 0);
                if (n <= 0) break;
                body.append(buf, static_cast<std::size_t>(n));
            }
            std::error_code ec;
            fs::create_directories(target->parent_path(), ec);
            const auto tmp = target->string() + ".part";  // rename, so the driver never sees a half-written file
            if (body.size() >= length && cfire::fs::write_text(tmp, std::string_view(body).substr(0, length))) {
                fs::rename(tmp, *target, ec);
                reply(c, 201, "Created");
            } else {
                reply(c, 500, "Write Failed");
            }
        } else {
            std::error_code ec;
            if (!fs::is_regular_file(*target, ec)) {
                reply(c, 404, "Not Found");
            } else if (method == "HEAD") {
                (void)send_all(c, std::format("HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n", fs::file_size(*target, ec)));
            } else {
                auto data = cfire::fs::read_text(*target);
                if (data) reply(c, 200, "OK", *data);
                else reply(c, 500, "Read Failed");
            }
        }
        ::close(c);
    }

    fs::path root_;
    int fd_ = -1;
    std::jthread thread_;
};

// True when something already answers HTTP on ip:port (an earlier driver's server).
auto port_answers(const std::string& ip, int port) -> bool {
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    ::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    const bool ok = ::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    ::close(s);
    return ok;
}

// An incremental, timestamp-preserving sync of the working tree into the share (pruning files the repo deleted,
// skipping unchanged files so Make/PCH timestamps stay intact), plus a tarball of it for Windows.
auto copy_tree(const fs::path& repo, const fs::path& share) -> std::optional<std::string> {
    auto ls = run_process({"git", "ls-files", "-co", "--exclude-standard", "-z"}, repo, 120);
    if (ls.exit_code != 0) return "git ls-files failed: " + ls.err;

    std::error_code ec;
    const fs::path dst = share / "cfire";
    fs::create_directories(dst, ec);
    if (ec) return std::format("create_directories {}: {}", dst.string(), ec.message());

    std::vector<fs::path> files;
    std::unordered_set<std::string> wanted;
    std::vector<std::string> manifest_lines;
    for (std::size_t start = 0; start < ls.out.size();) {
        auto end = ls.out.find('\0', start);
        if (end == std::string::npos) end = ls.out.size();
        const fs::path rel = ls.out.substr(start, end - start);
        start = end + 1;
        if (rel.empty() || rel.filename().string().starts_with("._") || !fs::is_regular_file(repo / rel, ec)) continue;  // deleted, or a symlink/dir
        std::string gen = rel.generic_string();
        if (wanted.insert(gen).second) {
            files.push_back(rel);
            manifest_lines.push_back(std::move(gen));
        }
    }

    // Prune deleted/unlisted files and any resulting empty directories from the shared copy.
    std::vector<fs::path> stale_files;
    std::vector<fs::path> dst_dirs;
    for (const auto& entry : fs::recursive_directory_iterator(dst, fs::directory_options::skip_permission_denied, ec)) {
        std::error_code sec;
        const fs::path rel = entry.path().lexically_relative(dst);
        const std::string gen = rel.generic_string();
        if (entry.is_directory(sec)) {
            dst_dirs.push_back(entry.path());
        } else if (gen != ".vmtest_manifest" && !wanted.contains(gen)) {
            stale_files.push_back(entry.path());
        }
    }
    for (const auto& p : stale_files) {
        fs::remove(p, ec);
    }
    std::ranges::sort(dst_dirs, [](const fs::path& a, const fs::path& b) {
        return a.native().size() > b.native().size();
    });
    for (const auto& d : dst_dirs) {
        if (fs::is_empty(d, ec)) fs::remove(d, ec);
    }

    std::size_t copied = 0;
    std::size_t unchanged = 0;
    for (const auto& rel : files) {
        const fs::path src_p = repo / rel;
        const fs::path dst_p = dst / rel;
        const auto src_sz = fs::file_size(src_p, ec);
        if (ec) return std::format("stat size {}: {}", rel.string(), ec.message());
        const auto src_mt = fs::last_write_time(src_p, ec);
        if (ec) return std::format("stat mtime {}: {}", rel.string(), ec.message());

        std::error_code dec;
        const bool same = fs::is_regular_file(dst_p, dec) && !dec &&
                          fs::file_size(dst_p, dec) == src_sz && !dec &&
                          fs::last_write_time(dst_p, dec) == src_mt && !dec;
        if (same) {
            ++unchanged;
            continue;
        }
        fs::create_directories(dst_p.parent_path(), ec);
        if (!fs::copy_file(src_p, dst_p, fs::copy_options::overwrite_existing, ec)) {
            return std::format("copy {}: {}", rel.string(), ec.message());
        }
        std::error_code mec;
        fs::last_write_time(dst_p, src_mt, mec);
        ++copied;
    }

    std::ranges::sort(manifest_lines);
    std::string manifest_text;
    for (const auto& line : manifest_lines) {
        manifest_text += line;
        manifest_text.push_back('\n');
    }
    const fs::path manifest_p = dst / ".vmtest_manifest";
    if (cfire::fs::read_text(manifest_p).value_or("") != manifest_text) {
        if (!cfire::fs::write_text(manifest_p, manifest_text)) {
            return "cannot write .vmtest_manifest";
        }
    }

    ::setenv("COPYFILE_DISABLE", "1", 1);  // no AppleDouble `._*` entries in the tarball
    auto tar = run_process({"tar", "--no-mac-metadata", "--no-xattrs", "-cf", "cfire.tar", "cfire"}, share, 600);
    if (tar.exit_code != 0) tar = run_process({"tar", "-cf", "cfire.tar", "cfire"}, share, 600);
    if (tar.exit_code != 0) return "tar failed: " + tar.err;
    std::println("==> synced working tree to {} ({} copied, {} unchanged)", dst.string(), copied, unchanged);
    return std::nullopt;
}

struct Utm {
    const vmtest::Config& cfg;
    auto run(std::vector<std::string> args, int timeout = 60) const -> ProcResult {
        args.insert(args.begin(), cfg.utmctl);
        return run_process(args, {}, timeout);
    }
    // Starts the VM if needed and waits until its guest agent reports an address.
    auto ready(const std::string& vm) const -> bool {
        if (!run({"status", vm}).out.contains("started")) {
            std::println("==> starting VM '{}'", vm);
            if (run({"start", vm}, 300).exit_code != 0) return false;
        }
        for (int i = 0; i < 90; ++i) {
            auto ip = run({"ip-address", vm}, 30);
            if (ip.exit_code == 0 && !ip.out.empty() && std::isdigit(static_cast<unsigned char>(ip.out[0]))) return true;
            std::this_thread::sleep_for(4s);
        }
        return false;
    }
    // The guest agent occasionally fails a single call ("The system cannot find the file specified"); retry it.
    auto exec(const std::string& vm, const std::vector<std::string>& cmd) const -> bool {
        std::vector<std::string> args{"exec", vm, "--cmd"};
        args.insert(args.end(), cmd.begin(), cmd.end());
        for (int attempt = 0; attempt < 3; ++attempt) {
            auto r = run(args, 120);
            if (r.exit_code == 0 && !r.timed_out) return true;
            std::this_thread::sleep_for(5s);
        }
        return false;
    }
};

auto wait_for(
    const fs::path& file,
    std::chrono::seconds limit,
    const std::function<void(double)>& on_heartbeat = {},
    const fs::path& hb_file = {},
    bool* hb_stale = nullptr
) -> bool {
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + limit;
    auto next_heartbeat = start + 30s;
    std::error_code ec;
    if (hb_stale) *hb_stale = false;
    while (std::chrono::steady_clock::now() < deadline) {
        if (fs::is_regular_file(file, ec) && fs::file_size(file, ec) > 0) return true;
        if (!hb_file.empty() && fs::is_regular_file(hb_file, ec) && !ec) {
            const auto wt = fs::last_write_time(hb_file, ec);
            if (!ec && fs::file_time_type::clock::now() - wt > 90s) {
                if (hb_stale) *hb_stale = true;
                return false;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (on_heartbeat && now >= next_heartbeat) {
            on_heartbeat(std::chrono::duration<double>(now - start).count());
            next_heartbeat = now + 30s;
        }
        std::this_thread::sleep_for(2s);
    }
    return false;
}

auto crlf(std::string_view s) -> std::string {
    std::string out;
    for (char ch : s) {
        if (ch == '\n') out += "\r\n";
        else out.push_back(ch);
    }
    return out;
}

struct GateOutcome {
    int rc = 1;
    double elapsed_s = 0.0;
    std::string log_output{};
    std::string error_msg{};
    std::string mode{};
    std::string vm{};
};

auto run_guest_gate(
    const vmtest::Config& cfg,
    const Utm& utm,
    std::string_view guest_mode,
    const std::string& job,
    const std::string& base,
    bool buffer_log,
    std::mutex& print_mu
) -> GateOutcome {
    const auto t0 = std::chrono::steady_clock::now();
    const bool linux_mode = (guest_mode == "linux" || guest_mode == "setup-linux");
    const std::string& vm = linux_mode ? cfg.vm_linux : cfg.vm_windows;
    GateOutcome out;
    out.mode = std::string(guest_mode);
    out.vm = vm;

    auto log_line = [&](std::string_view msg) {
        std::lock_guard<std::mutex> lk(print_mu);
        std::println("{}", msg);
    };

    if (!utm.ready(vm)) {
        out.error_msg = std::format("VM '{}' did not come up with its guest agent", vm);
        out.elapsed_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return out;
    }

    std::error_code ec;
    const fs::path results = cfg.share / "results";
    const fs::path hb_path = results / (job + ".hb");
    fs::remove(results / (job + ".log"), ec);
    fs::remove(results / (job + ".rc"), ec);
    fs::remove(results / (job + ".task"), ec);
    fs::remove(hb_path, ec);

    bool launched = false;
    if (linux_mode) {
        const std::string body = guest_mode == "setup-linux"
            ? "export DEBIAN_FRONTEND=noninteractive; apt-get update -q && apt-get install -y -q clang libclang-rt-dev make git pkg-config rsync curl cmake "
              "libzstd-dev libsimdjson-dev libsqlite3-dev libsodium-dev libssh-dev libssl-dev libasound2-dev libgtk-3-dev libwebkit2gtk-4.1-dev"
            : std::format("bash /mnt/vm-shared/cfire/scripts/vm/linux_gate.sh {} {} {}", cfg.share_tag, cfg.host_ip, cfg.port);
        const std::string wrapper = std::format(
            "mkdir -p /mnt/vm-shared; mountpoint -q /mnt/vm-shared || mount -t 9p -o trans=virtio,version=9p2000.L,msize=262144 {0} /mnt/vm-shared\n"
            "( while :; do date +%s > /var/tmp/{2}.hb; curl -s -T /var/tmp/{2}.hb {3}/results/{2}.hb >/dev/null 2>&1 || true; sleep 15; done ) &\n"
            "HB_PID=$!\n"
            "trap 'kill $HB_PID 2>/dev/null || true' EXIT\n"
            "( {1} ) > /var/tmp/{2}.log 2>&1; echo $? > /var/tmp/{2}.rc\n"
            "kill $HB_PID 2>/dev/null || true\n"
            "curl -s -T /var/tmp/{2}.log {3}/results/{2}.log; curl -s -T /var/tmp/{2}.rc {3}/results/{2}.rc\n",
            cfg.share_tag, body, job, base);
        const std::string b64 = cfire::detail::base64_encode(std::span(reinterpret_cast<const std::uint8_t*>(wrapper.data()), wrapper.size()));
        launched = utm.exec(vm, {"/bin/sh", "-c",
                                 std::format("echo {0} | base64 -d > /var/tmp/{1}.sh; "
                                             "if command -v systemd-run >/dev/null 2>&1; then "
                                             "systemd-run --no-block --unit=cfire-vm-{1} --collect /bin/bash /var/tmp/{1}.sh >/dev/null 2>&1 || nohup /bin/bash /var/tmp/{1}.sh >/dev/null 2>&1 & "
                                             "else nohup /bin/bash /var/tmp/{1}.sh >/dev/null 2>&1 & fi",
                                             b64, job)});
    } else {
        const std::string run = guest_mode == "setup-windows"
            ? std::format("curl -s -o C:\\cfire-work\\setup.cmd {0}/cfire/scripts/vm/windows_setup.cmd\n"
                          "call C:\\cfire-work\\setup.cmd > C:\\cfire-work\\{1}.log 2>&1\n"
                          "echo %ERRORLEVEL% > C:\\cfire-work\\{1}.rc",
                          base, job)
            : std::format("set MSYSTEM=CLANG64\nset CHERE_INVOKING=1\n"
                          "C:\\msys64\\usr\\bin\\bash.exe -lc \"curl -s -o /c/cfire-work/gate.sh {0}/cfire/scripts/vm/windows_gate.sh; "
                          "bash /c/cfire-work/gate.sh {1} {2} > /c/cfire-work/{3}.log 2>&1; echo $? > /c/cfire-work/{3}.rc\"",
                          base, cfg.host_ip, cfg.port, job);
        // After the results, the job deletes its own task and reports whether the task is really gone.
        const std::string script = std::format(
            "@echo off\n{0}\n"
            "curl -s -T C:\\cfire-work\\{1}.log {2}/results/{1}.log\n"
            "curl -s -T C:\\cfire-work\\{1}.rc {2}/results/{1}.rc\n"
            "schtasks /delete /tn cfire_vm_{1} /f\n"
            "schtasks /query /tn cfire_vm_{1} >nul 2>&1 && (echo present> C:\\cfire-work\\{1}.task) || (echo gone> C:\\cfire-work\\{1}.task)\n"
            "curl -s -T C:\\cfire-work\\{1}.task {2}/results/{1}.task\n",
            run, job, base);
        if (!cfire::fs::write_text(cfg.share / "jobs" / (job + ".cmd"), crlf(script))) {
            out.error_msg = "cannot write the job script";
            out.elapsed_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            return out;
        }
        // A scheduled task outlives the guest-agent call (a plain `start /b` child does not). Each job gets its own
        // task name, since `schtasks /run` silently does nothing while a task of the same name is still running.
        launched = utm.exec(vm, {"C:\\Windows\\System32\\cmd.exe", "/c",
                                 std::format("mkdir C:\\cfire-work 2>nul & curl -s -o C:\\cfire-work\\{0}.cmd {1}/jobs/{0}.cmd & "
                                             "schtasks /create /tn cfire_vm_{0} /tr C:\\cfire-work\\{0}.cmd /sc once /st 23:59 /ru SYSTEM /f & "
                                             "schtasks /run /tn cfire_vm_{0}",
                                             job, base)});
    }
    if (!launched) {
        out.error_msg = std::format("utmctl exec failed three times on '{}'", vm);
        out.elapsed_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return out;
    }

    log_line(std::format("==> {} running in '{}' (job {}); waiting for results (at most {}s)", guest_mode, vm, job, cfg.timeout_s));
    bool hb_stale = false;
    const bool ready_rc = wait_for(
        results / (job + ".rc"),
        std::chrono::seconds(cfg.timeout_s),
        [&](double elapsed) {
            if (buffer_log) {
                log_line(std::format("==> [{}] still running in '{}' ({:.0f}s elapsed)...", guest_mode, vm, elapsed));
            }
        },
        linux_mode ? hb_path : fs::path{},
        &hb_stale
    );
    fs::remove(hb_path, ec);
    if (!ready_rc) {
        out.error_msg = hb_stale
            ? std::format("guest job in '{}' stopped heartbeating for >90s without uploading .rc (job {})", vm, job)
            : std::format("no result from '{}' after {}s (job {})", vm, cfg.timeout_s, job);
        out.elapsed_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return out;
    }

    std::this_thread::sleep_for(1s);  // the log is uploaded before the exit code, but give a slow guest a moment
    if (auto log = cfire::fs::read_text(results / (job + ".log"))) {
        std::string_view rest = *log;
        while (!rest.empty()) {
            const auto nl = rest.find('\n');
            std::string_view line = rest.substr(0, nl);
            rest = nl == std::string_view::npos ? std::string_view{} : rest.substr(nl + 1);
            if (line.ends_with('\r')) line.remove_suffix(1);
            if (!line.contains("LIBARCHIVE.xattr")) {
                if (buffer_log) {
                    out.log_output.append(line);
                    out.log_output.push_back('\n');
                } else {
                    std::println("{}", line);
                }
            }
        }
    }
    std::string rc_text = cfire::fs::read_text(results / (job + ".rc")).value_or("");
    std::erase_if(rc_text, [](char ch) { return !std::isdigit(static_cast<unsigned char>(ch)); });
    out.rc = rc_text.empty() ? 1 : std::atoi(rc_text.c_str());

    if (!linux_mode) {
        if (!wait_for(results / (job + ".task"), 120s)) {
            const std::string wmsg = std::format("warning: the Windows job did not report its task cleanup (cfire_vm_{})", job);
            if (buffer_log) out.log_output += wmsg + "\n";
            else std::println("{}", wmsg);
        } else if (cfire::fs::read_text(results / (job + ".task")).value_or("").contains("present")) {
            const std::string wmsg = std::format("warning: scheduled task cfire_vm_{} is still registered in the guest", job);
            if (buffer_log) out.log_output += wmsg + "\n";
            else std::println("{}", wmsg);
        }
    }

    out.elapsed_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (buffer_log) {
        log_line(std::format("==> [{}] finished in '{}' with exit code {} ({:.1f}s)", guest_mode, vm, out.rc, out.elapsed_s));
    }
    return out;
}

} // namespace

auto main(int argc, char** argv) -> int {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // progress lines show up as they happen, also under `make ... > log`
    const std::string mode = argc > 1 ? argv[1] : "";
    const bool both_mode = mode == "both";
    const bool linux_mode = mode == "linux" || mode == "setup-linux";
    if (!both_mode && !linux_mode && mode != "windows" && mode != "setup-windows") {
        std::println(stderr, "usage: {} linux|windows|both|setup-linux|setup-windows", argc > 0 ? argv[0] : ".vmtest");
        return 2;
    }
    const auto cfg = vmtest::load_config(vmtest::process_env);
    const fs::path repo = fs::current_path();
    std::error_code ec;
    if (!fs::exists(cfg.utmctl, ec)) return die(std::format("utmctl not found at {} (install UTM, or set CFIRE_UTMCTL)", cfg.utmctl));
    fs::create_directories(cfg.share / "results", ec);
    fs::create_directories(cfg.share / "jobs", ec);

    std::unique_ptr<RunLock> lock_linux;
    std::unique_ptr<RunLock> lock_windows;
    if (both_mode || linux_mode) {
        const std::string lname = ".running_" + (both_mode ? std::string("linux") : mode);
        lock_linux = std::make_unique<RunLock>(cfg.share / lname);
        if (!lock_linux->acquire()) {
            return die(std::format("another '{}' run is in progress (its lock is {})",
                                   both_mode ? "linux" : mode, (cfg.share / lname).string()));
        }
    }
    if (both_mode || !linux_mode) {
        const std::string wname = ".running_" + (both_mode ? std::string("windows") : mode);
        lock_windows = std::make_unique<RunLock>(cfg.share / wname);
        if (!lock_windows->acquire()) {
            return die(std::format("another '{}' run is in progress (its lock is {})",
                                   both_mode ? "windows" : mode, (cfg.share / wname).string()));
        }
    }

    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    const std::string ts = std::format("{:%Y%m%d_%H%M%S}", now);
    const std::string base = std::format("http://{}:{}", cfg.host_ip, cfg.port);

    if (auto err = copy_tree(repo, cfg.share)) return die(*err);

    FileServer server(cfg.share);
    bool in_use = false;
    if (!server.start(cfg.host_ip, cfg.port, in_use)) {
        if (!(in_use && port_answers(cfg.host_ip, cfg.port)))
            return die(std::format("cannot serve on {}:{} (is UTM's shared network up? set CFIRE_VM_HOST_IP / CFIRE_VM_PORT)", cfg.host_ip, cfg.port));
        std::println("==> reusing the file server already on {}:{}", cfg.host_ip, cfg.port);
    }

    const Utm utm{cfg};
    std::mutex print_mu;

    if (both_mode) {
        GateOutcome lin_res;
        GateOutcome win_res;
        {
            std::jthread t_lin([&] {
                lin_res = run_guest_gate(cfg, utm, "linux", "linux_" + ts, base, true, print_mu);
            });
            std::jthread t_win([&] {
                win_res = run_guest_gate(cfg, utm, "windows", "windows_" + ts, base, true, print_mu);
            });
        }
        std::println("\n=== [VM: Linux] ({:.1f}s, rc={}) ===", lin_res.elapsed_s, lin_res.rc);
        if (!lin_res.error_msg.empty()) std::println(stderr, "error: {}", lin_res.error_msg);
        if (!lin_res.log_output.empty()) std::print("{}", lin_res.log_output);

        std::println("\n=== [VM: Windows] ({:.1f}s, rc={}) ===", win_res.elapsed_s, win_res.rc);
        if (!win_res.error_msg.empty()) std::println(stderr, "error: {}", win_res.error_msg);
        if (!win_res.log_output.empty()) std::print("{}", win_res.log_output);

        std::println("==> concurrent VM gates finished: linux rc={} ({:.1f}s), windows rc={} ({:.1f}s)",
                     lin_res.rc, lin_res.elapsed_s, win_res.rc, win_res.elapsed_s);
        return (lin_res.rc == 0 && win_res.rc == 0) ? 0 : 1;
    }

    const std::string job = std::format("{}_{}", mode, ts);
    GateOutcome res = run_guest_gate(cfg, utm, mode, job, base, false, print_mu);
    if (!res.error_msg.empty()) return die(res.error_msg);
    std::println("==> {} finished in '{}' with exit code {}", mode, res.vm, res.rc);
    return res.rc;
}
#endif
