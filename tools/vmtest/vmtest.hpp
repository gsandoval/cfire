#pragma once
// tools/vmtest/vmtest.hpp — the pure parts of the UTM VM gate driver (CF-187): settings and the file server's
// request rules. tools/vmtest/main.cpp does the I/O; tests/runner_self_tests.hpp unit-tests this header.

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace vmtest {

// Every setting has a default and a CFIRE_VM_* override, so other machines can use their own VM names and network.
struct Config {
    std::string utmctl = "/Applications/UTM.app/Contents/MacOS/utmctl";
    std::string vm_linux = "Linux";
    std::string vm_windows = "Windows";
    std::string host_ip = "192.168.64.1";  // the host as seen from the guests (UTM's shared network)
    int port = 8765;
    std::filesystem::path share{};         // a host folder the Linux guest mounts with the 9p tag below
    std::string share_tag = "share";
    int timeout_s = 10800;                 // a whole gate run
};

using EnvLookup = std::function<std::optional<std::string>(std::string_view)>;

[[nodiscard]] inline auto process_env(std::string_view name) -> std::optional<std::string> {
    const char* v = std::getenv(std::string(name).c_str());
    if (v == nullptr || *v == '\0') return std::nullopt;
    return std::string(v);
}

[[nodiscard]] inline auto load_config(const EnvLookup& env) -> Config {
    Config c;
    if (auto v = env("CFIRE_UTMCTL")) c.utmctl = *v;
    if (auto v = env("CFIRE_VM_LINUX")) c.vm_linux = *v;
    if (auto v = env("CFIRE_VM_WINDOWS")) c.vm_windows = *v;
    if (auto v = env("CFIRE_VM_HOST_IP")) c.host_ip = *v;
    auto num = [&](std::string_view name, int fallback) {
        if (auto v = env(name)) {
            try {
                const int n = std::stoi(*v);
                if (n > 0) return n;
            } catch (...) {
            }
        }
        return fallback;
    };
    c.port = num("CFIRE_VM_PORT", c.port);
    c.timeout_s = num("CFIRE_VM_TIMEOUT", c.timeout_s);
    if (auto v = env("CFIRE_VM_SHARE")) c.share = *v;
    else c.share = std::filesystem::path(env("HOME").value_or(".")) / "Programming" / "vm-shared";
    if (auto v = env("CFIRE_VM_SHARE_TAG")) c.share_tag = *v;
    return c;
}

// The file server serves the share read-only and accepts uploads only as `PUT /results/<name>`. Returns the file the
// request may touch, or nullopt to refuse it (403): no `..`, no hidden names, no absolute or backslash tricks.
[[nodiscard]] inline auto resolve_request(const std::filesystem::path& root, std::string_view method, std::string_view url)
    -> std::optional<std::filesystem::path> {
    if (const auto q = url.find_first_of("?#"); q != std::string_view::npos) url = url.substr(0, q);
    if (!url.starts_with('/') || url.contains('\\') || url.contains('%') || url.contains('\0')) return std::nullopt;
    std::string_view rest = url.substr(1);
    std::filesystem::path rel;
    std::size_t parts = 0;
    while (!rest.empty()) {
        const auto slash = rest.find('/');
        const std::string_view part = rest.substr(0, slash);
        if (part.empty() || part.starts_with('.') || part.contains(':')) return std::nullopt;
        rel /= std::string(part);
        ++parts;
        rest = slash == std::string_view::npos ? std::string_view{} : rest.substr(slash + 1);
    }
    if (parts == 0) return std::nullopt;
    if (method == "PUT") {
        if (parts != 2 || rel.begin()->string() != "results") return std::nullopt;
    } else if (method != "GET" && method != "HEAD") {
        return std::nullopt;
    }
    return root / rel;
}

} // namespace vmtest
