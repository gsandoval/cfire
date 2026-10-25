// src/server.hpp — Native zero-dependency HTTP/1.1 server for WebAssembly and static web assets
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <print>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace cfire::serve {

inline auto get_mime_type(const std::filesystem::path& path) -> std::string_view {
    auto ext = path.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(c));
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".wasm") return "application/wasm";
    if (ext == ".js" || ext == ".mjs") return "application/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".data") return "application/octet-stream";
    if (ext == ".txt" || ext == ".md") return "text/plain; charset=utf-8";
    if (ext == ".mp3") return "audio/mpeg";
    if (ext == ".ogg") return "audio/ogg";
    if (ext == ".wav") return "audio/wav";
    return "application/octet-stream";
}

inline auto url_decode(std::string_view in) -> std::string {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            auto hex_val = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int h = hex_val(in[i + 1]);
            int l = hex_val(in[i + 2]);
            if (h >= 0 && l >= 0) {
                out += static_cast<char>((h << 4) | l);
                i += 2;
                continue;
            }
        } else if (in[i] == '+') {
            out += ' ';
            continue;
        }
        out += in[i];
    }
    return out;
}

inline auto sanitize_relative_path(std::string_view req_path) -> std::optional<std::filesystem::path> {
    while (!req_path.empty() && (req_path.front() == '/' || req_path.front() == '\\')) {
        req_path.remove_prefix(1);
    }
    if (req_path.empty()) return std::filesystem::path(".");
    std::filesystem::path p(req_path);
    for (const auto& part : p) {
        if (part == "..") return std::nullopt;
    }
    return p;
}

inline void open_in_browser(const std::string& url) {
#if defined(__APPLE__)
    std::string cmd = "open " + url;
#elif defined(_WIN32)
    std::string cmd = "start " + url;
#else
    std::string cmd = "xdg-open " + url;
#endif
    (void)std::system(cmd.c_str());
}

inline void send_all(int fd, const char* data, std::size_t len) {
    std::size_t total = 0;
    while (total < len) {
#if defined(_WIN32)
        int sent = ::send(fd, data + total, static_cast<int>(len - total), 0);
#else
        ssize_t sent = ::send(fd, data + total, len - total, 0);
#endif
        if (sent <= 0) break;
        total += static_cast<std::size_t>(sent);
    }
}

inline void handle_client(int client_fd, const std::filesystem::path& root_dir, const std::string& default_entry) {
    char buf[4096];
#if defined(_WIN32)
    int n = ::recv(client_fd, buf, sizeof(buf) - 1, 0);
#else
    ssize_t n = ::recv(client_fd, buf, sizeof(buf) - 1, 0);
#endif
    if (n <= 0) {
#if defined(_WIN32)
        ::closesocket(client_fd);
#else
        ::close(client_fd);
#endif
        return;
    }
    buf[n] = '\0';

    std::string_view req(buf, static_cast<std::size_t>(n));
    auto line_end = req.find("\r\n");
    if (line_end == std::string_view::npos) line_end = req.find('\n');
    if (line_end == std::string_view::npos) {
#if defined(_WIN32)
        ::closesocket(client_fd);
#else
        ::close(client_fd);
#endif
        return;
    }

    std::string_view first_line = req.substr(0, line_end);
    auto sp1 = first_line.find(' ');
    if (sp1 == std::string_view::npos) {
#if defined(_WIN32)
        ::closesocket(client_fd);
#else
        ::close(client_fd);
#endif
        return;
    }
    auto sp2 = first_line.find(' ', sp1 + 1);
    if (sp2 == std::string_view::npos) sp2 = first_line.size();

    std::string method(first_line.substr(0, sp1));
    std::string raw_uri(first_line.substr(sp1 + 1, sp2 - sp1 - 1));

    // Strip query string
    auto qpos = raw_uri.find('?');
    if (qpos != std::string::npos) {
        raw_uri = raw_uri.substr(0, qpos);
    }

    std::string decoded_uri = url_decode(raw_uri);
    auto rel_opt = sanitize_relative_path(decoded_uri);

    if (!rel_opt || (method != "GET" && method != "HEAD")) {
        std::string resp = (method != "GET" && method != "HEAD")
            ? "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
            : "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send_all(client_fd, resp.data(), resp.size());
#if defined(_WIN32)
        ::closesocket(client_fd);
#else
        ::close(client_fd);
#endif
        return;
    }

    std::filesystem::path target_path = root_dir / (*rel_opt);

    // If requested path is root or directory
    if (std::filesystem::is_directory(target_path)) {
        if (!default_entry.empty() && std::filesystem::exists(root_dir / default_entry)) {
            target_path = root_dir / default_entry;
        } else if (std::filesystem::exists(target_path / "index.html")) {
            target_path = target_path / "index.html";
        } else {
            // Find any *.html file in directory
            bool found_html = false;
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(target_path, ec)) {
                if (entry.is_regular_file() && entry.path().extension() == ".html") {
                    target_path = entry.path();
                    found_html = true;
                    break;
                }
            }
            if (!found_html) {
                // Return directory listing
                std::string html = "<html><head><meta charset='utf-8'><title>Directory Index</title></head><body><h2>Files</h2><ul>";
                for (const auto& entry : std::filesystem::directory_iterator(target_path, ec)) {
                    auto name = entry.path().filename().string();
                    html += std::format("<li><a href=\"/{}\">{}</a></li>", name, name);
                }
                html += "</ul></body></html>";
                std::string resp = std::format(
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html; charset=utf-8\r\n"
                    "Content-Length: {}\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Cross-Origin-Opener-Policy: same-origin\r\n"
                    "Cross-Origin-Embedder-Policy: require-corp\r\n"
                    "Cache-Control: no-cache, no-store, must-revalidate\r\n"
                    "Connection: close\r\n\r\n{}",
                    html.size(), html);
                send_all(client_fd, resp.data(), resp.size());
#if defined(_WIN32)
                ::closesocket(client_fd);
#else
                ::close(client_fd);
#endif
                return;
            }
        }
    }

    if (!std::filesystem::exists(target_path) || !std::filesystem::is_regular_file(target_path)) {
        std::string not_found = "<html><body><h1>404 Not Found</h1></body></html>";
        std::string resp = std::format(
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: {}\r\n"
            "Connection: close\r\n\r\n{}",
            not_found.size(), not_found);
        send_all(client_fd, resp.data(), resp.size());
#if defined(_WIN32)
        ::closesocket(client_fd);
#else
        ::close(client_fd);
#endif
        return;
    }

    auto mime = get_mime_type(target_path);
    auto file_sz = std::filesystem::file_size(target_path);

    std::string header = std::format(
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: {}\r\n"
        "Content-Length: {}\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Cross-Origin-Opener-Policy: same-origin\r\n"
        "Cross-Origin-Embedder-Policy: require-corp\r\n"
        "Cache-Control: no-cache, no-store, must-revalidate\r\n"
        "Connection: close\r\n\r\n",
        mime, file_sz);

    send_all(client_fd, header.data(), header.size());

    if (method == "GET") {
        std::ifstream file(target_path, std::ios::binary);
        if (file) {
            char fbuf[16384];
            while (file.read(fbuf, sizeof(fbuf)) || file.gcount() > 0) {
                send_all(client_fd, fbuf, static_cast<std::size_t>(file.gcount()));
            }
        }
    }

    std::println("  [HTTP] {} {} -> 200 OK ({}, {} bytes)", method, decoded_uri, mime, file_sz);

#if defined(_WIN32)
    ::closesocket(client_fd);
#else
    ::close(client_fd);
#endif
}

inline auto run_server(std::filesystem::path target, int requested_port = 8080, bool open_browser = true) -> int {
#if defined(_WIN32)
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::println(stderr, "error: WSAStartup failed");
        return 1;
    }
#else
    signal(SIGPIPE, SIG_IGN);
#endif

    std::filesystem::path root_dir;
    std::string default_entry;

    if (target.empty() || target == ".") {
        root_dir = std::filesystem::current_path();
    } else if (std::filesystem::is_directory(target)) {
        root_dir = std::filesystem::canonical(target);
    } else if (std::filesystem::is_regular_file(target)) {
        root_dir = std::filesystem::canonical(target.parent_path());
        default_entry = target.filename().string();
    } else {
        // Maybe relative path
        if (std::filesystem::exists(target)) {
            if (std::filesystem::is_directory(target)) {
                root_dir = std::filesystem::canonical(target);
            } else {
                root_dir = std::filesystem::canonical(target.parent_path());
                default_entry = target.filename().string();
            }
        } else {
            root_dir = std::filesystem::current_path();
        }
    }

    // Try finding default entry if unset
    if (default_entry.empty()) {
        if (std::filesystem::exists(root_dir / "index.html")) {
            default_entry = "index.html";
        } else if (std::filesystem::exists(root_dir / "flautas-web.html")) {
            default_entry = "flautas-web.html";
        } else {
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(root_dir, ec)) {
                if (entry.is_regular_file() && entry.path().extension() == ".html") {
                    default_entry = entry.path().filename().string();
                    break;
                }
            }
        }
    }

    int server_fd = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (server_fd < 0) {
        std::println(stderr, "error: failed to create socket");
        return 1;
    }

    int opt = 1;
#if defined(_WIN32)
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    int active_port = requested_port;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    bool bound = false;
    for (int p = requested_port; p < requested_port + 25; ++p) {
        addr.sin_port = htons(static_cast<uint16_t>(p));
        if (::bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            active_port = p;
            bound = true;
            break;
        }
    }

    if (!bound) {
        std::println(stderr, "error: failed to bind socket on ports {}-{}", requested_port, requested_port + 24);
#if defined(_WIN32)
        ::closesocket(server_fd);
        WSACleanup();
#else
        ::close(server_fd);
#endif
        return 1;
    }

    if (::listen(server_fd, 64) < 0) {
        std::println(stderr, "error: listen failed on port {}", active_port);
#if defined(_WIN32)
        ::closesocket(server_fd);
        WSACleanup();
#else
        ::close(server_fd);
#endif
        return 1;
    }

    std::string initial_url = std::format("http://localhost:{}", active_port);
    if (!default_entry.empty()) {
        initial_url += "/" + default_entry;
    }

    std::println("==> cfire WebAssembly & Static HTTP Server");
    std::println("    Serving directory: {}", root_dir.string());
    std::println("    Active URL:        {}", initial_url);
    std::println("    Features:          application/wasm MIME, COOP/COEP isolation headers");
    std::println("    Press Ctrl+C to terminate server.\n");

    if (open_browser) {
        open_in_browser(initial_url);
    }

    while (true) {
        sockaddr_in client_addr{};
#if defined(_WIN32)
        int client_len = sizeof(client_addr);
#else
        socklen_t client_len = sizeof(client_addr);
#endif
        int client_fd = static_cast<int>(::accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len));
        if (client_fd < 0) {
            break;
        }
        std::thread([client_fd, root_dir, default_entry]() {
            handle_client(client_fd, root_dir, default_entry);
        }).detach();
    }

#if defined(_WIN32)
    ::closesocket(server_fd);
    WSACleanup();
#else
    ::close(server_fd);
#endif
    return 0;
}

} // namespace cfire::serve
