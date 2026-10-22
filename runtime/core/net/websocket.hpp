#pragma once
#include "http.hpp"
#include "../encoding.hpp"

#include <span>
#include <vector>
#include <string>
#include <string_view>
#include <expected>
#include <unordered_map>
#include <optional>
#include <memory>
#include <random>
#include <utility>
#include <type_traits>
#include <cctype>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

// --- `core:net/websocket` (RFC 6455 High-Level WebSockets & WSS Client & Server) ---
namespace cfire::websocket {

namespace detail {

inline auto rol(std::uint32_t value, std::size_t bits) noexcept -> std::uint32_t {
    return (value << bits) | (value >> (32 - bits));
}

inline auto sha1(std::string_view input) -> std::vector<std::uint8_t> {
    std::uint32_t h0 = 0x67452301;
    std::uint32_t h1 = 0xEFCDAB89;
    std::uint32_t h2 = 0x98BADCFE;
    std::uint32_t h3 = 0x10325476;
    std::uint32_t h4 = 0xC3D2E1F0;

    std::vector<std::uint8_t> msg(input.begin(), input.end());
    std::uint64_t bit_len = msg.size() * 8;
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) {
        msg.push_back(0x00);
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<std::uint8_t>((bit_len >> (i * 8)) & 0xFF));
    }

    for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        std::uint32_t w[80];
        for (std::size_t i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(msg[chunk + i * 4]) << 24) |
                   (static_cast<std::uint32_t>(msg[chunk + i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(msg[chunk + i * 4 + 2]) << 8) |
                   (static_cast<std::uint32_t>(msg[chunk + i * 4 + 3]));
        }
        for (std::size_t i = 16; i < 80; ++i) {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        std::uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (std::size_t i = 0; i < 80; ++i) {
            std::uint32_t f = 0, k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            std::uint32_t temp = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = temp;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    std::vector<std::uint8_t> digest(20);
    for (int i = 0; i < 4; ++i) {
        digest[i]      = (h0 >> ((3 - i) * 8)) & 0xFF;
        digest[i + 4]  = (h1 >> ((3 - i) * 8)) & 0xFF;
        digest[i + 8]  = (h2 >> ((3 - i) * 8)) & 0xFF;
        digest[i + 12] = (h3 >> ((3 - i) * 8)) & 0xFF;
        digest[i + 16] = (h4 >> ((3 - i) * 8)) & 0xFF;
    }
    return digest;
}

inline auto recv_exact(
    int fd,
    void* buf,
    std::size_t len,
    const std::shared_ptr<cfire::net::TlsStream>& tls = nullptr
) -> bool {
    std::size_t received = 0;
    auto* ptr = static_cast<std::uint8_t*>(buf);
    while (received < len) {
        ssize_t n = (tls && tls->ssl && tls->read_fn)
            ? tls->read_fn(tls->ssl, ptr + received, len - received)
            : ::recv(fd, ptr + received, len - received, 0);
        if (n <= 0) return false;
        received += static_cast<std::size_t>(n);
    }
    return true;
}

inline auto send_all(
    int fd,
    const void* buf,
    std::size_t len,
    const std::shared_ptr<cfire::net::TlsStream>& tls = nullptr
) -> bool {
    std::size_t sent_total = 0;
    const auto* ptr = static_cast<const std::uint8_t*>(buf);
    while (sent_total < len) {
        ssize_t n = (tls && tls->ssl && tls->write_fn)
            ? tls->write_fn(tls->ssl, ptr + sent_total, len - sent_total)
            : ::send(fd, ptr + sent_total, len - sent_total, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent_total += static_cast<std::size_t>(n);
    }
    return true;
}

} // namespace detail

[[nodiscard]] inline auto compute_accept_key(std::string_view client_key) -> std::string {
    static constexpr std::string_view kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = std::string(client_key) + std::string(kGuid);
    auto digest = detail::sha1(combined);
    return cfire::base64::encode(std::span<const std::byte>(reinterpret_cast<const std::byte*>(digest.data()), digest.size()));
}

[[nodiscard]] inline auto generate_nonce() -> std::string {
    std::uint8_t nonce[16];
    std::random_device rd;
    for (int i = 0; i < 16; ++i) {
        nonce[i] = static_cast<std::uint8_t>(rd() & 0xFF);
    }
    return cfire::base64::encode(std::span<const std::byte>(reinterpret_cast<const std::byte*>(nonce), 16));
}

struct WsFrame {
    std::uint8_t opcode = 0x1;
    bool is_final = true;
    std::vector<std::uint8_t> payload{};
};

[[nodiscard]] inline auto encode_frame(const WsFrame& f, bool mask) -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> out;
    std::uint8_t b0 = (f.is_final ? 0x80 : 0x00) | (f.opcode & 0x0F);
    out.push_back(b0);

    std::size_t len = f.payload.size();
    std::uint8_t mask_bit = mask ? 0x80 : 0x00;

    if (len < 126) {
        out.push_back(mask_bit | static_cast<std::uint8_t>(len));
    } else if (len <= 0xFFFF) {
        out.push_back(mask_bit | 126);
        out.push_back(static_cast<std::uint8_t>((len >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>(len & 0xFF));
    } else {
        out.push_back(mask_bit | 127);
        for (int i = 7; i >= 0; --i) {
            out.push_back(static_cast<std::uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }

    if (mask) {
        std::uint8_t mask_key[4];
        std::random_device rd;
        for (int i = 0; i < 4; ++i) {
            mask_key[i] = static_cast<std::uint8_t>(rd() & 0xFF);
            out.push_back(mask_key[i]);
        }
        for (std::size_t i = 0; i < len; ++i) {
            out.push_back(f.payload[i] ^ mask_key[i % 4]);
        }
    } else {
        out.insert(out.end(), f.payload.begin(), f.payload.end());
    }

    return out;
}

inline constexpr std::size_t kDefaultMaxFrameSize = 16 * 1024 * 1024; // 16 MiB

[[nodiscard]] inline auto decode_frame(
    int fd,
    const std::shared_ptr<cfire::net::TlsStream>& tls = nullptr,
    std::size_t max_frame_size = kDefaultMaxFrameSize
) -> std::optional<WsFrame> {
    std::uint8_t header[2];
    if (!detail::recv_exact(fd, header, 2, tls)) return std::nullopt;

    WsFrame f;
    f.is_final = (header[0] & 0x80) != 0;
    f.opcode = header[0] & 0x0F;
    bool has_mask = (header[1] & 0x80) != 0;
    std::uint64_t len = header[1] & 0x7F;

    if (len == 126) {
        std::uint8_t ext[2];
        if (!detail::recv_exact(fd, ext, 2, tls)) return std::nullopt;
        len = (static_cast<std::uint64_t>(ext[0]) << 8) | ext[1];
    } else if (len == 127) {
        std::uint8_t ext[8];
        if (!detail::recv_exact(fd, ext, 8, tls)) return std::nullopt;
        len = 0;
        for (int i = 0; i < 8; ++i) len = (len << 8) | ext[i];
        // RFC 6455 Section 5.2: most significant bit of 64-bit length MUST be 0
        if ((len & (1ULL << 63)) != 0) return std::nullopt;
    }

    if (len > max_frame_size) {
        return std::nullopt;
    }

    std::uint8_t mask_key[4] = {0};
    if (has_mask) {
        if (!detail::recv_exact(fd, mask_key, 4, tls)) return std::nullopt;
    }

    if (len > 0) {
        static constexpr std::size_t kChunkSize = 65536;
        f.payload.reserve(std::min<std::size_t>(static_cast<std::size_t>(len), kChunkSize));
        std::size_t remaining = static_cast<std::size_t>(len);
        while (remaining > 0) {
            std::size_t chunk_len = std::min<std::size_t>(remaining, kChunkSize);
            std::size_t prev_size = f.payload.size();
            f.payload.resize(prev_size + chunk_len);
            if (!detail::recv_exact(fd, f.payload.data() + prev_size, chunk_len, tls)) {
                return std::nullopt;
            }
            remaining -= chunk_len;
        }
    }

    if (has_mask) {
        for (std::size_t i = 0; i < len; ++i) {
            f.payload[i] ^= mask_key[i % 4];
        }
    }

    return f;
}

class Conn {
public:
    explicit Conn(int fd = -1, bool is_client = true, std::shared_ptr<cfire::net::TlsStream> tls = nullptr) noexcept
        : fd_(fd), is_client_(is_client), tls_(std::move(tls)) {
        if (fd_ >= 0) {
            // Clear the 2-second HTTP header timeout so long-lived WebSocket connections do not time out when idle
            timeval tv{.tv_sec = 0, .tv_usec = 0};
            ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            ::setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#ifdef SO_NOSIGPIPE
            int opt = 1;
            ::setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
#endif
        }
    }
    ~Conn() { close(); }
    Conn(Conn&& o) noexcept
        : fd_(std::exchange(o.fd_, -1)),
          is_client_(o.is_client_),
          tls_(std::move(o.tls_)) {}
    auto operator=(Conn&& o) noexcept -> Conn& {
        if (this != &o) {
            close();
            fd_ = std::exchange(o.fd_, -1);
            is_client_ = o.is_client_;
            tls_ = std::move(o.tls_);
        }
        return *this;
    }
    Conn(const Conn&) = delete;
    auto operator=(const Conn&) = delete;

    [[nodiscard]] auto is_open() const noexcept -> bool { return fd_ >= 0; }
    [[nodiscard]] auto is_tls() const noexcept -> bool { return tls_ != nullptr && tls_->ssl != nullptr; }
    [[nodiscard]] auto fd() const noexcept -> int { return fd_; }
    void set_max_frame_size(std::size_t max_bytes) noexcept { max_frame_size_ = max_bytes; }
    [[nodiscard]] auto max_frame_size() const noexcept -> std::size_t { return max_frame_size_; }

    void set_timeout_ms(int ms) noexcept {
        if (fd_ < 0) return;
        timeval tv{};
        if (ms > 0) {
            tv.tv_sec = ms / 1000;
            tv.tv_usec = (ms % 1000) * 1000;
        }
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ::setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }

    auto send_text(std::string_view msg) -> std::expected<void, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("websocket connection is closed");
        WsFrame f{
            .opcode = 0x1,
            .is_final = true,
            .payload = std::vector<std::uint8_t>(msg.begin(), msg.end())
        };
        auto bytes = encode_frame(f, is_client_);
        if (!detail::send_all(fd_, bytes.data(), bytes.size(), tls_)) {
            return cfire::make_error("failed to send websocket text frame");
        }
        return {};
    }

    auto send_binary(std::span<const std::uint8_t> data) -> std::expected<void, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("websocket connection is closed");
        WsFrame f{
            .opcode = 0x2,
            .is_final = true,
            .payload = std::vector<std::uint8_t>(data.begin(), data.end())
        };
        auto bytes = encode_frame(f, is_client_);
        if (!detail::send_all(fd_, bytes.data(), bytes.size(), tls_)) {
            return cfire::make_error("failed to send websocket binary frame");
        }
        return {};
    }

    auto send_binary(std::span<const std::byte> data) -> std::expected<void, cfire::Error> {
        return send_binary(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(data.data()), data.size()));
    }

    auto ping(std::string_view payload = "") -> std::expected<void, cfire::Error> {
        if (fd_ < 0) return cfire::make_error("websocket connection is closed");
        WsFrame f{
            .opcode = 0x9,
            .is_final = true,
            .payload = std::vector<std::uint8_t>(payload.begin(), payload.end())
        };
        auto bytes = encode_frame(f, is_client_);
        if (!detail::send_all(fd_, bytes.data(), bytes.size(), tls_)) {
            return cfire::make_error("failed to send websocket ping frame");
        }
        return {};
    }

    auto recv_frame() -> std::expected<WsFrame, cfire::Error> {
        while (fd_ >= 0) {
            auto f_opt = decode_frame(fd_, tls_, max_frame_size_);
            if (!f_opt) {
                tls_.reset();
                if (fd_ >= 0) ::close(std::exchange(fd_, -1));
                return cfire::make_error("websocket connection closed by peer");
            }
            auto& f = *f_opt;
            if (f.opcode == 0x8) { // Close
                tls_.reset();
                if (fd_ >= 0) ::close(std::exchange(fd_, -1));
                return cfire::make_error("websocket closed by peer");
            }
            if (f.opcode == 0x9) { // Ping -> auto-reply with Pong
                WsFrame pong{
                    .opcode = 0xA,
                    .is_final = true,
                    .payload = std::move(f.payload)
                };
                auto bytes = encode_frame(pong, is_client_);
                (void)detail::send_all(fd_, bytes.data(), bytes.size(), tls_);
                continue;
            }
            if (f.opcode == 0xA) { // Pong
                continue;
            }
            return f;
        }
        return cfire::make_error("websocket connection is closed");
    }

    auto recv_text() -> std::expected<std::string, cfire::Error> {
        auto frame = recv_frame();
        if (!frame) return std::unexpected(frame.error());
        if (frame->opcode != 0x1 && frame->opcode != 0x0) {
            return cfire::make_error("expected text frame (opcode 0x1)");
        }
        return std::string(frame->payload.begin(), frame->payload.end());
    }

    auto recv_binary() -> std::expected<std::vector<std::uint8_t>, cfire::Error> {
        auto frame = recv_frame();
        if (!frame) return std::unexpected(frame.error());
        if (frame->opcode != 0x2 && frame->opcode != 0x0) {
            return cfire::make_error("expected binary frame (opcode 0x2)");
        }
        return std::move(frame->payload);
    }

    void close(std::uint16_t code = 1000, std::string_view reason = "") {
        if (fd_ >= 0) {
            int sock = std::exchange(fd_, -1);
            std::vector<std::uint8_t> payload;
            payload.push_back(static_cast<std::uint8_t>((code >> 8) & 0xFF));
            payload.push_back(static_cast<std::uint8_t>(code & 0xFF));
            payload.insert(payload.end(), reason.begin(), reason.end());
            WsFrame f{.opcode = 0x8, .is_final = true, .payload = std::move(payload)};
            auto bytes = encode_frame(f, is_client_);
            (void)detail::send_all(sock, bytes.data(), bytes.size(), tls_);
            tls_.reset();
            ::close(sock);
        }
    }

private:
    int fd_ = -1;
    bool is_client_ = true;
    std::shared_ptr<cfire::net::TlsStream> tls_{nullptr};
    std::size_t max_frame_size_ = kDefaultMaxFrameSize;
};

[[nodiscard]] inline auto from_fd(int fd, bool is_client = true) -> Conn {
    return Conn(fd, is_client);
}

[[nodiscard]] inline auto pair() -> std::expected<std::pair<Conn, Conn>, cfire::Error> {
    int sv[2]{-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        return cfire::make_error("socketpair failed");
    }
    return std::make_pair(Conn(sv[0], /*is_client=*/true), Conn(sv[1], /*is_client=*/false));
}

[[nodiscard]] inline auto upgrade(
    int client_fd,
    const std::unordered_map<std::string, std::string>& headers,
    std::shared_ptr<cfire::net::TlsStream> tls = nullptr
) -> std::expected<Conn, cfire::Error> {
    std::string client_key;
    for (const auto& [k, v] : headers) {
        std::string lk;
        for (char c : k) lk += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lk == "sec-websocket-key") {
            client_key = v;
            break;
        }
    }
    if (client_key.empty()) {
        return cfire::make_error("missing Sec-WebSocket-Key header in websocket upgrade request");
    }
    std::string accept = compute_accept_key(client_key);
    std::string resp = std::format(
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: {}\r\n\r\n",
        accept
    );
    if (!detail::send_all(client_fd, resp.data(), resp.size(), tls)) {
        return cfire::make_error("failed to send websocket upgrade response");
    }
    return Conn(client_fd, /*is_client=*/false, std::move(tls));
}

[[nodiscard]] inline auto upgrade(cfire::net::TcpConn& tcp, const std::unordered_map<std::string, std::string>& headers) -> std::expected<Conn, cfire::Error> {
    int fd = tcp.release_fd();
    auto tls = tcp.release_tls();
    return upgrade(fd, headers, std::move(tls));
}

[[nodiscard]] inline auto upgrade(const cfire::http::Request& req, cfire::net::TcpConn& tcp) -> std::expected<Conn, cfire::Error> {
    return upgrade(tcp, req.headers);
}

[[nodiscard]] inline auto connect(std::string_view url_str, bool verify_peer = true) -> std::expected<Conn, cfire::Error> {
    auto u = cfire::url::parse(url_str);
    if (!u) return std::unexpected(u.error());
    std::uint16_t port = u->port != 0 ? u->port : ((u->scheme == "wss" || u->scheme == "https") ? 443 : 80);

    auto tcp_res = cfire::net::connect_tcp(u->host, port);
    if (!tcp_res) return std::unexpected(tcp_res.error());
    auto& tcp = *tcp_res;

    if (u->scheme == "wss" || u->scheme == "https") {
        auto tls_res = cfire::tls::wrap_client(tcp, u->host, verify_peer);
        if (!tls_res) return std::unexpected(tls_res.error());
    }

    std::string nonce = generate_nonce();
    std::string expected_accept = compute_accept_key(nonce);

    std::string path = u->path.empty() ? "/" : u->path;
    if (!u->query.empty()) path += "?" + u->query;

    std::string req;
    req += std::format("GET {} HTTP/1.1\r\n", path);
    req += std::format("Host: {}\r\n", u->host);
    req += "Upgrade: websocket\r\n";
    req += "Connection: Upgrade\r\n";
    req += std::format("Sec-WebSocket-Key: {}\r\n", nonce);
    req += "Sec-WebSocket-Version: 13\r\n\r\n";

    auto wr = tcp.write(req);
    if (!wr) return std::unexpected(wr.error());

    std::string resp_buf;
    while (!resp_buf.contains("\r\n\r\n")) {
        auto chunk = tcp.read_str(1024);
        if (!chunk || chunk->empty()) return cfire::make_error("websocket handshake aborted by server");
        resp_buf += *chunk;
    }

    if (!resp_buf.starts_with("HTTP/1.1 101") && !resp_buf.starts_with("HTTP/1.0 101")) {
        return cfire::make_error("websocket handshake failed: server did not return 101 Switching Protocols");
    }

    auto parsed_resp = cfire::http::parse_response(resp_buf);
    if (parsed_resp) {
        std::string accept_hdr = parsed_resp->header("sec-websocket-accept");
        if (!accept_hdr.empty() && accept_hdr != expected_accept) {
            return cfire::make_error("websocket handshake failed: Sec-WebSocket-Accept key mismatch");
        }
    }

    int raw_fd = tcp.release_fd();
    auto tls = tcp.release_tls();
    return Conn(raw_fd, /*is_client=*/true, std::move(tls));
}

[[nodiscard]] inline auto connect_insecure(std::string_view url_str) -> std::expected<Conn, cfire::Error> {
    return connect(url_str, /*verify_peer=*/false);
}

class Server {
public:
    explicit Server(cfire::net::TcpListener listener, std::optional<cfire::tls::ServerContext> tls_ctx = std::nullopt)
        : listener_(std::move(listener)), tls_ctx_(std::move(tls_ctx)) {}
    ~Server() {
        stop();
    }

    Server(Server&& o) noexcept
        : listener_(std::move(o.listener_)),
          tls_ctx_(std::move(o.tls_ctx_)),
          running_(o.running_.load()) {
        o.running_ = false;
        if (o.worker_.joinable()) {
            worker_ = std::move(o.worker_);
        }
    }

    auto operator=(Server&& o) noexcept -> Server& {
        if (this != &o) {
            stop();
            listener_ = std::move(o.listener_);
            tls_ctx_ = std::move(o.tls_ctx_);
            running_ = o.running_.load();
            o.running_ = false;
            if (o.worker_.joinable()) {
                worker_ = std::move(o.worker_);
            }
        }
        return *this;
    }

    [[nodiscard]] auto port() const noexcept -> std::uint16_t { return listener_.port(); }
    [[nodiscard]] auto is_tls() const noexcept -> bool { return tls_ctx_.has_value() && tls_ctx_->valid(); }

    auto enable_tls(cfire::tls::ServerContext ctx) -> Server& {
        tls_ctx_ = std::move(ctx);
        return *this;
    }

    auto enable_tls_files(std::string_view cert_file, std::string_view key_file)
        -> std::expected<void, cfire::Error>
    {
        auto ctx_res = cfire::tls::ServerContext::from_files(cert_file, key_file);
        if (!ctx_res) return std::unexpected(ctx_res.error());
        tls_ctx_ = std::move(*ctx_res);
        return {};
    }

    auto enable_tls_pem(std::string_view cert_pem, std::string_view key_pem)
        -> std::expected<void, cfire::Error>
    {
        auto ctx_res = cfire::tls::ServerContext::from_pem(cert_pem, key_pem);
        if (!ctx_res) return std::unexpected(ctx_res.error());
        tls_ctx_ = std::move(*ctx_res);
        return {};
    }

    void stop() noexcept {
        running_ = false;
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    [[nodiscard]] auto accept() -> std::expected<Conn, cfire::Error> {
        auto conn_res = listener_.accept();
        if (!conn_res) return std::unexpected(conn_res.error());
        auto& conn = *conn_res;

        if (tls_ctx_.has_value() && tls_ctx_->valid()) {
            auto wr = tls_ctx_->wrap_server(conn);
            if (!wr) return std::unexpected(wr.error());
        }

        auto req_res = cfire::http::read_http_request(conn);
        if (!req_res) {
            cfire::http::Response err_resp = cfire::http::Response::bad_request("400 Bad Request");
            (void)conn.write(err_resp.to_http_wire());
            return std::unexpected(req_res.error());
        }

        auto ws_res = upgrade(*req_res, conn);
        if (!ws_res) {
            cfire::http::Response err_resp = cfire::http::Response::bad_request(ws_res.error().message());
            (void)conn.write(err_resp.to_http_wire());
            return std::unexpected(ws_res.error());
        }
        return std::move(*ws_res);
    }

    template <typename Handler>
    void serve_once(Handler&& handler) {
        auto conn_res = listener_.accept();
        if (!conn_res) return;
        handle_conn(*conn_res, handler);
    }

    template <typename Handler>
    void listen_and_serve(Handler&& handler) {
        running_ = true;
        while (running_) {
            auto conn_res = listener_.accept();
            if (!conn_res) {
                if (!running_) break;
                continue;
            }
            handle_conn(*conn_res, handler);
        }
    }

    template <typename Handler>
    void serve_in_background(Handler handler) {
        stop();
        running_ = true;
        worker_ = std::thread([this, h = std::move(handler)]() mutable {
            this->listen_and_serve(h);
        });
    }

private:
    template <typename Handler>
    void handle_conn(cfire::net::TcpConn& conn, Handler& handler) {
        if (tls_ctx_.has_value() && tls_ctx_->valid()) {
            auto wr = tls_ctx_->wrap_server(conn);
            if (!wr) return;
        }
        auto req_res = cfire::http::read_http_request(conn);
        if (!req_res) {
            cfire::http::Response err_resp = cfire::http::Response::bad_request("400 Bad Request");
            (void)conn.write(err_resp.to_http_wire());
            return;
        }
        auto ws_res = upgrade(*req_res, conn);
        if (!ws_res) {
            cfire::http::Response err_resp = cfire::http::Response::bad_request(ws_res.error().message());
            (void)conn.write(err_resp.to_http_wire());
            return;
        }
        if constexpr (std::is_invocable_v<Handler, const cfire::http::Request&, Conn&>) {
            handler(*req_res, *ws_res);
        } else {
            handler(*ws_res);
        }
    }

    cfire::net::TcpListener listener_;
    std::optional<cfire::tls::ServerContext> tls_ctx_{std::nullopt};
    std::atomic<bool> running_{false};
    std::thread worker_{};
};

[[nodiscard]] inline auto make_server(std::uint16_t port = 0)
    -> std::expected<Server, cfire::Error>
{
    auto l = cfire::net::listen_tcp(port);
    if (!l) return std::unexpected(l.error());
    return Server(std::move(*l));
}

[[nodiscard]] inline auto make_tls_server(
    std::uint16_t port,
    std::string_view cert_file,
    std::string_view key_file
) -> std::expected<Server, cfire::Error> {
    auto ctx_res = cfire::tls::ServerContext::from_files(cert_file, key_file);
    if (!ctx_res) return std::unexpected(ctx_res.error());
    auto l = cfire::net::listen_tcp(port);
    if (!l) return std::unexpected(l.error());
    return Server(std::move(*l), std::move(*ctx_res));
}

[[nodiscard]] inline auto make_tls_server_pem(
    std::uint16_t port,
    std::string_view cert_pem,
    std::string_view key_pem
) -> std::expected<Server, cfire::Error> {
    auto ctx_res = cfire::tls::ServerContext::from_pem(cert_pem, key_pem);
    if (!ctx_res) return std::unexpected(ctx_res.error());
    auto l = cfire::net::listen_tcp(port);
    if (!l) return std::unexpected(l.error());
    return Server(std::move(*l), std::move(*ctx_res));
}

} // namespace cfire::websocket
