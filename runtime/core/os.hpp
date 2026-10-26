#pragma once
#include "../cfire_prelude.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <expected>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#if !TARGET_OS_IPHONE
#include <crt_externs.h>
#endif
#endif

namespace cfire::os {

[[nodiscard]] inline auto getenv(std::string_view key, std::string_view fallback = "") -> std::string {
    std::string k(key);
    if (const char* v = std::getenv(k.c_str())) {
        return std::string(v);
    }
    return std::string(fallback);
}

inline void setenv(std::string_view key, std::string_view val) {
    std::string k(key);
    std::string v(val);
    ::setenv(k.c_str(), v.c_str(), 1);
}

[[nodiscard]] inline auto cwd() -> std::filesystem::path {
    return std::filesystem::current_path();
}

[[nodiscard]] inline auto exec(std::string_view cmd) -> std::expected<std::string, cfire::Error> {
    std::string c(cmd);
    FILE* pipe = ::popen((c + " 2>&1").c_str(), "r");
    if (!pipe) {
        return cfire::make_error(std::format("failed to spawn command: {}", cmd));
    }
    std::string out;
    char buf[2048];
    while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
        out.append(buf);
    }
    int status = ::pclose(pipe);
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : status;
    if (code != 0) {
        return cfire::make_error(std::format("command exited with {}: {}", code, out));
    }
    return out;
}

[[nodiscard]] inline auto exec_output(std::string_view cmd) -> std::expected<std::string, cfire::Error> {
    return exec(cmd);
}

struct ExecResult {
    int exit_code = 0;
    std::string stdout_str{};
    std::string stderr_str{};
    [[nodiscard]] auto success() const noexcept -> bool { return exit_code == 0; }
};

[[nodiscard]] inline auto command(std::string_view binary, std::span<const std::string> args = {})
    -> std::expected<ExecResult, cfire::Error>
{
    int stdout_pipe[2]{-1, -1};
    int stderr_pipe[2]{-1, -1};
    if (::pipe(stdout_pipe) != 0 || ::pipe(stderr_pipe) != 0) {
        if (stdout_pipe[0] >= 0) { ::close(stdout_pipe[0]); ::close(stdout_pipe[1]); }
        if (stderr_pipe[0] >= 0) { ::close(stderr_pipe[0]); ::close(stderr_pipe[1]); }
        return cfire::make_error("failed to create subprocess pipes");
    }

    posix_spawn_file_actions_t actions;
    ::posix_spawn_file_actions_init(&actions);
    ::posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
    ::posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);
    ::posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);
    ::posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO);
    ::posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);
    ::posix_spawn_file_actions_addclose(&actions, stderr_pipe[1]);

    std::vector<char*> argv_ptrs;
    std::string bin_str(binary);
    argv_ptrs.push_back(bin_str.data());
    std::vector<std::string> args_storage(args.begin(), args.end());
    for (auto& a : args_storage) {
        argv_ptrs.push_back(a.data());
    }
    argv_ptrs.push_back(nullptr);

    pid_t pid = 0;
#if defined(__APPLE__) && !TARGET_OS_IPHONE
    char** env = *_NSGetEnviron();
#else
    extern char** environ;
    char** env = environ;
#endif

    int spawn_rc = ::posix_spawnp(&pid, bin_str.c_str(), &actions, nullptr, argv_ptrs.data(), env);
    ::posix_spawn_file_actions_destroy(&actions);
    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[1]);

    if (spawn_rc != 0) {
        ::close(stdout_pipe[0]);
        ::close(stderr_pipe[0]);
        return cfire::make_error(std::format("posix_spawnp failed for '{}': {}", binary, std::strerror(spawn_rc)));
    }

    ExecResult res;
    auto read_pipe = [](int fd) -> std::string {
        std::string out;
        char buf[4096];
        ssize_t n = 0;
        while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
            out.append(buf, static_cast<std::size_t>(n));
        }
        ::close(fd);
        return out;
    };
    res.stdout_str = read_pipe(stdout_pipe[0]);
    res.stderr_str = read_pipe(stderr_pipe[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);
    res.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : status;
    return res;
}

[[nodiscard]] inline auto command(std::string_view binary, std::initializer_list<std::string_view> args)
    -> std::expected<ExecResult, cfire::Error>
{
    std::vector<std::string> vec;
    for (auto a : args) vec.emplace_back(a);
    return command(binary, std::span<const std::string>(vec));
}

} // namespace cfire::os

