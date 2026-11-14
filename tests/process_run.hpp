#pragma once
// tests/process_run.hpp — Run a program with an argv, a working directory and a timeout, capturing stdout and stderr
// separately (CF-186). The conformance and GUIDE runners need both, which `run_command_capture` (one merged stream,
// no timeout) cannot give. Output goes to files rather than pipes, so a chatty child can never block on a full pipe.

#include "../runtime/cfire_core_stdlib.hpp"
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#endif
#endif

struct ProcResult {
    int exit_code = 0;       // the exit status; -N when killed by signal N (POSIX)
    bool timed_out = false;  // killed after the timeout
    bool spawn_failed = false;
    std::string out{};
    std::string err{};
};

namespace proc_detail {
inline auto slurp(const std::filesystem::path& p) -> std::string {
    auto text = cfire::fs::read_text(p);
    return text ? std::string(*text) : std::string();
}
inline auto capture_paths() -> std::pair<std::filesystem::path, std::filesystem::path> {
    static std::atomic<std::uint64_t> s_id{0};
    auto id = s_id++;
#if defined(_WIN32)
    auto pid = static_cast<unsigned long>(::GetCurrentProcessId());
#else
    auto pid = static_cast<long>(::getpid());
#endif
    auto base = std::filesystem::temp_directory_path() / std::format("cfire_proc_{}_{}", pid, id);
    return {base.string() + ".out", base.string() + ".err"};
}
} // namespace proc_detail

[[nodiscard]] inline auto run_process(const std::vector<std::string>& argv, const std::filesystem::path& cwd, int timeout_seconds)
    -> ProcResult {
    ProcResult r;
    if (argv.empty()) {
        r.spawn_failed = true;
        return r;
    }
    auto [out_path, err_path] = proc_detail::capture_paths();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
#if defined(_WIN32)
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE out_h = ::CreateFileW(out_path.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE err_h = ::CreateFileW(err_path.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    std::wstring cmd_line;
    for (const auto& a : argv) {
        if (!cmd_line.empty()) cmd_line.push_back(L' ');
        cmd_line += cfire::os::detail::quote_win32_arg(cfire::os::detail::win32_utf8_to_wide(a));
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = out_h;
    si.hStdError = err_h;
    si.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    // A job object, so a timeout also ends grandchildren (cfire run -> the compiled program).
    HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        ::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }
    std::wstring wcwd = cwd.wstring();
    const BOOL started = ::CreateProcessW(nullptr, cmd_line.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr,
                                          wcwd.empty() ? nullptr : wcwd.c_str(), &si, &pi);
    ::CloseHandle(out_h);
    ::CloseHandle(err_h);
    if (!started) {
        if (job) ::CloseHandle(job);
        r.spawn_failed = true;
        r.exit_code = -1;
    } else {
        if (job) ::AssignProcessToJobObject(job, pi.hProcess);
        ::ResumeThread(pi.hThread);
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (::WaitForSingleObject(pi.hProcess, static_cast<DWORD>(std::max<long long>(remaining, 0))) == WAIT_TIMEOUT) {
            r.timed_out = true;
            if (job) ::TerminateJobObject(job, 124);
            else ::TerminateProcess(pi.hProcess, 124);
            ::WaitForSingleObject(pi.hProcess, 5000);
        }
        DWORD code = 0;
        ::GetExitCodeProcess(pi.hProcess, &code);
        r.exit_code = static_cast<int>(code);
        if (job) ::CloseHandle(job);
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
    }
#else
    posix_spawn_file_actions_t actions;
    ::posix_spawn_file_actions_init(&actions);
    ::posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, out_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    ::posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, err_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (!cwd.empty()) {
#if defined(__APPLE__) && defined(__MAC_OS_X_VERSION_MIN_REQUIRED) && __MAC_OS_X_VERSION_MIN_REQUIRED >= 260000
        ::posix_spawn_file_actions_addchdir(&actions, cwd.c_str());  // the POSIX 2024 name; `_np` is deprecated in macOS 26
#else
        ::posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
#endif
    }
    // Its own process group, so a timeout also ends grandchildren (cfire run -> the compiled program).
    posix_spawnattr_t attr;
    ::posix_spawnattr_init(&attr);
    ::posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    ::posix_spawnattr_setpgroup(&attr, 0);
    std::vector<std::string> storage(argv);
    std::vector<char*> ptrs;
    for (auto& a : storage) ptrs.push_back(a.data());
    ptrs.push_back(nullptr);
#if defined(__APPLE__)
    char** env = *_NSGetEnviron();
#else
    char** env = ::environ;
#endif
    pid_t pid = 0;
    const int rc = ::posix_spawnp(&pid, ptrs[0], &actions, &attr, ptrs.data(), env);
    ::posix_spawn_file_actions_destroy(&actions);
    ::posix_spawnattr_destroy(&attr);
    if (rc != 0) {
        r.spawn_failed = true;
        r.exit_code = -1;
        r.err = std::format("could not start '{}': {}", argv[0], std::strerror(rc));
    } else {
        // Wait without reaping (WNOWAIT), so the process-group id stays reserved until the group is killed: the
        // group may outlive the direct child (a program `cfire run` started), and must not be left running.
        auto nap = std::chrono::milliseconds(2);
        while (true) {
            siginfo_t info{};
            const int w = ::waitid(P_PID, static_cast<id_t>(pid), &info, WEXITED | WNOHANG | WNOWAIT);
            if (w == 0 && info.si_pid == pid) break;
            if (w < 0 && errno != EINTR) break;
            if (std::chrono::steady_clock::now() >= deadline) {
                r.timed_out = true;
                break;
            }
            std::this_thread::sleep_for(nap);
            nap = std::min(nap * 2, std::chrono::milliseconds(25));
        }
        ::kill(-pid, SIGKILL);
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        if (WIFEXITED(status)) r.exit_code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) r.exit_code = -WTERMSIG(status);
    }
#endif
    if (!r.spawn_failed) {
        r.out = proc_detail::slurp(out_path);
        r.err = proc_detail::slurp(err_path);
    }
    std::error_code ec;
    std::filesystem::remove(out_path, ec);
    std::filesystem::remove(err_path, ec);
    return r;
}

// Runs `n` independent jobs on up to `jobs` threads; job(i) writes only its own result slot.
template <class F>
inline void parallel_for_each_index(std::size_t n, std::size_t jobs, F&& job) {
    std::atomic<std::size_t> next{0};
    std::vector<std::jthread> pool;
    const std::size_t workers = std::max<std::size_t>(1, std::min(jobs, n));
    for (std::size_t w = 0; w < workers; ++w) {
        pool.emplace_back([&] {
            for (std::size_t i = next++; i < n; i = next++) job(i);
        });
    }
}

[[nodiscard]] inline auto default_jobs() -> std::size_t {
    const unsigned hc = std::thread::hardware_concurrency();
    return std::max<std::size_t>(1, std::min<std::size_t>(8, (hc == 0 ? 2 : hc) / 2));
}
