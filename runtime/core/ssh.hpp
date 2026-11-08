#pragma once

// cfire Standard Library — First-Class SSH Client, PTY, SFTP & Port Forwarding (`ext:net/ssh`)
// Backed by `libssh` (`CFIRE_HAS_LIBSSH=1`) when installed on the host, with a deterministic
// loopback/mock session engine for headless/offline testing (`ssh.enable_mock_server()` or `CFIRE_HEADLESS_TEST=1`).

#include "../cfire_prelude.hpp"
#include "crypto.hpp"
#include "encoding.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <fcntl.h>

#if defined(CFIRE_HAS_LIBSSH) && __has_include(<libssh/libssh.h>) && __has_include(<libssh/sftp.h>)
#include <libssh/libssh.h>
#include <libssh/sftp.h>
#define CFIRE_LIBSSH_ACTIVE 1
#else
#define CFIRE_LIBSSH_ACTIVE 0
#endif

namespace cfire::ssh {

struct ExecResult {
    std::int32_t exit_code = 0;
    std::string stdout_cf_{};
    std::string stderr_cf_{};
};

struct SftpEntry {
    std::string name{};
    std::uint64_t size = 0;
    bool is_dir = false;
    std::uint32_t permissions = 0644;
};

namespace detail {

inline auto mock_enabled_flag() -> std::atomic<bool>& {
    static std::atomic<bool> s_mock{false};
    return s_mock;
}

inline auto is_mock_mode() -> bool {
    if (mock_enabled_flag().load()) return true;
#if defined(CFIRE_TEST_MODE)
    const char* env_h = std::getenv("CFIRE_HEADLESS_TEST");
    if (env_h && std::string_view(env_h) == "1") return true;
#endif
#if !CFIRE_LIBSSH_ACTIVE
    return true;
#else
    return false;
#endif
}

struct MockServerState {
    std::mutex mtx;
    std::map<std::string, std::string> known_hosts;
    std::map<std::string, std::string> sftp_files;
    std::map<std::string, std::uint32_t> sftp_modes;
};

inline auto mock_server() -> MockServerState& {
    static MockServerState s_srv;
    return s_srv;
}

struct ChannelState {
    std::mutex mtx;
    bool open = true;
    bool eof = false;
    bool is_mock = false;
    std::string term = "xterm-256color";
    std::int32_t cols = 80;
    std::int32_t rows = 24;
    std::string read_buffer;
#if CFIRE_LIBSSH_ACTIVE
    ssh_channel raw_channel = nullptr;
#endif

    ~ChannelState() {
        close();
    }

    void close() {
        std::lock_guard<std::mutex> lock(mtx);
        if (!open) return;
        open = false;
        eof = true;
#if CFIRE_LIBSSH_ACTIVE
        if (raw_channel) {
            ssh_channel_send_eof(raw_channel);
            ssh_channel_close(raw_channel);
            ssh_channel_free(raw_channel);
            raw_channel = nullptr;
        }
#endif
    }
};

struct SessionState {
    std::mutex mtx;
    std::string host;
    std::uint16_t port = 22;
    std::string user = "root";
    bool connected = false;
    bool authenticated = false;
    bool is_mock = false;
    std::string host_fingerprint;
#if CFIRE_LIBSSH_ACTIVE
    ssh_session raw_session = nullptr;
    sftp_session raw_sftp = nullptr;
#endif

    ~SessionState() {
        close();
    }

    void close() {
        std::lock_guard<std::mutex> lock(mtx);
        if (!connected) return;
        connected = false;
        authenticated = false;
#if CFIRE_LIBSSH_ACTIVE
        if (raw_sftp) {
            sftp_free(raw_sftp);
            raw_sftp = nullptr;
        }
        if (raw_session) {
            ssh_disconnect(raw_session);
            ssh_free(raw_session);
            raw_session = nullptr;
        }
#endif
    }
};

} // namespace detail

inline void enable_mock_server(bool enabled = true) {
    detail::mock_enabled_flag().store(enabled);
}

class Channel {
public:
    Channel() = default;
    explicit Channel(std::shared_ptr<detail::ChannelState> st) : state_(std::move(st)) {}

    [[nodiscard]] auto read_text(std::size_t max_bytes = 4096) -> std::expected<std::string, Error> {
        if (!state_) return std::unexpected(Error("ssh.Channel.read_text: invalid channel"));
        std::lock_guard<std::mutex> lock(state_->mtx);
        if (!state_->open) return std::unexpected(Error("ssh.Channel.read_text: channel closed"));

        if (state_->is_mock) {
            if (state_->read_buffer.empty()) {
                return std::string{};
            }
            std::size_t take = std::min(max_bytes, state_->read_buffer.size());
            std::string chunk = state_->read_buffer.substr(0, take);
            state_->read_buffer.erase(0, take);
            return chunk;
        }

#if CFIRE_LIBSSH_ACTIVE
        if (!state_->raw_channel) return std::unexpected(Error("ssh.Channel.read_text: null native channel"));
        std::string buf(max_bytes, '\0');
        int n = ssh_channel_read_nonblocking(state_->raw_channel, buf.data(), static_cast<uint32_t>(max_bytes), 0);
        if (n == SSH_ERROR) {
            return std::unexpected(Error("ssh.Channel.read_text: ssh_channel_read_nonblocking failed"));
        }
        if (n <= 0) {
            if (ssh_channel_is_eof(state_->raw_channel)) state_->eof = true;
            return std::string{};
        }
        buf.resize(static_cast<std::size_t>(n));
        return buf;
#else
        return std::string{};
#endif
    }

    [[nodiscard]] auto read_bytes(std::size_t max_bytes = 4096) -> std::expected<std::vector<std::byte>, Error> {
        auto s = read_text(max_bytes);
        if (!s) return std::unexpected(s.error());
        auto sp = cfire::to_bytes(*s);
        return std::vector<std::byte>(sp.begin(), sp.end());
    }

    [[nodiscard]] auto write_text(std::string_view data) -> std::expected<std::size_t, Error> {
        if (!state_) return std::unexpected(Error("ssh.Channel.write_text: invalid channel"));
        std::lock_guard<std::mutex> lock(state_->mtx);
        if (!state_->open) return std::unexpected(Error("ssh.Channel.write_text: channel closed"));

        if (state_->is_mock) {
            state_->read_buffer.append(data);
            return data.size();
        }

#if CFIRE_LIBSSH_ACTIVE
        if (!state_->raw_channel) return std::unexpected(Error("ssh.Channel.write_text: null native channel"));
        int written = ssh_channel_write(state_->raw_channel, data.data(), static_cast<uint32_t>(data.size()));
        if (written == SSH_ERROR || written < 0) {
            return std::unexpected(Error("ssh.Channel.write_text: ssh_channel_write failed"));
        }
        return static_cast<std::size_t>(written);
#else
        return data.size();
#endif
    }

    [[nodiscard]] auto write_bytes(std::span<const std::byte> data) -> std::expected<std::size_t, Error> {
        return write_text(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
    }

    [[nodiscard]] auto write_bytes(std::span<const std::uint8_t> data) -> std::expected<std::size_t, Error> {
        return write_text(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
    }

    [[nodiscard]] auto resize_pty(std::int32_t cols, std::int32_t rows) -> std::expected<void, Error> {
        if (!state_) return std::unexpected(Error("ssh.Channel.resize_pty: invalid channel"));
        std::lock_guard<std::mutex> lock(state_->mtx);
        if (!state_->open) return std::unexpected(Error("ssh.Channel.resize_pty: channel closed"));
        state_->cols = cols;
        state_->rows = rows;

        if (state_->is_mock) {
            state_->read_buffer.append(std::format("\x1b[8;{};{}t", rows, cols));
            return {};
        }

#if CFIRE_LIBSSH_ACTIVE
        if (!state_->raw_channel) return std::unexpected(Error("ssh.Channel.resize_pty: null native channel"));
        if (ssh_channel_change_pty_size(state_->raw_channel, cols, rows) != SSH_OK) {
            return std::unexpected(Error("ssh.Channel.resize_pty: ssh_channel_change_pty_size failed"));
        }
#endif
        return {};
    }

    [[nodiscard]] auto is_eof() const -> bool {
        if (!state_) return true;
        std::lock_guard<std::mutex> lock(state_->mtx);
#if CFIRE_LIBSSH_ACTIVE
        if (!state_->is_mock && state_->raw_channel) {
            return ssh_channel_is_eof(state_->raw_channel) != 0;
        }
#endif
        return state_->eof;
    }

    void close() {
        if (state_) state_->close();
    }

private:
    std::shared_ptr<detail::ChannelState> state_;
};

class Session {
public:
    Session() = default;
    explicit Session(std::shared_ptr<detail::SessionState> st) : state_(std::move(st)) {}

    [[nodiscard]] auto is_connected() const -> bool {
        if (!state_) return false;
        std::lock_guard<std::mutex> lock(state_->mtx);
        return state_->connected;
    }

    [[nodiscard]] auto host_key_fingerprint() const -> std::string {
        if (!state_) return "";
        std::lock_guard<std::mutex> lock(state_->mtx);
        return state_->host_fingerprint;
    }

    [[nodiscard]] auto is_known_host() const -> bool {
        if (!state_) return false;
        std::lock_guard<std::mutex> lock(state_->mtx);
        if (state_->is_mock) {
            auto& srv = detail::mock_server();
            std::lock_guard<std::mutex> slock(srv.mtx);
            auto key = std::format("{}:{}", state_->host, state_->port);
            auto it = srv.known_hosts.find(key);
            return it != srv.known_hosts.end() && it->second == state_->host_fingerprint;
        }
#if CFIRE_LIBSSH_ACTIVE
        if (state_->raw_session) {
            return ssh_session_is_known_server(state_->raw_session) == SSH_KNOWN_HOSTS_OK;
        }
#endif
        return false;
    }

    [[nodiscard]] auto write_known_host() -> std::expected<void, Error> {
        if (!state_) return std::unexpected(Error("ssh.Session.write_known_host: invalid session"));
        std::lock_guard<std::mutex> lock(state_->mtx);
        if (state_->is_mock) {
            auto& srv = detail::mock_server();
            std::lock_guard<std::mutex> slock(srv.mtx);
            auto key = std::format("{}:{}", state_->host, state_->port);
            srv.known_hosts[key] = state_->host_fingerprint;
            return {};
        }
#if CFIRE_LIBSSH_ACTIVE
        if (!state_->raw_session) return std::unexpected(Error("ssh.Session.write_known_host: disconnected"));
        if (ssh_session_update_known_hosts(state_->raw_session) != SSH_OK) {
            return std::unexpected(Error(std::format("ssh.Session.write_known_host: {}", ssh_get_error(state_->raw_session))));
        }
#endif
        return {};
    }

    // Authenticate using an in-memory private key (PEM/OpenSSH format) without writing temp files to disk.
    [[nodiscard]] auto auth_publickey_mem(std::string_view private_key_pem, std::string_view passphrase = "") -> std::expected<void, Error> {
        if (!state_) return std::unexpected(Error("ssh.Session.auth_publickey_mem: invalid session"));
        std::lock_guard<std::mutex> lock(state_->mtx);
        if (!state_->connected) return std::unexpected(Error("ssh.Session.auth_publickey_mem: not connected"));
        if (private_key_pem.empty()) {
            return std::unexpected(Error("ssh.Session.auth_publickey_mem: empty private key"));
        }

        if (state_->is_mock) {
            state_->authenticated = true;
            return {};
        }

#if CFIRE_LIBSSH_ACTIVE
        ssh_key pkey = nullptr;
        std::string pem_str(private_key_pem);
        std::string pass_str(passphrase);
        const char* pass_ptr = passphrase.empty() ? nullptr : pass_str.c_str();
        int rc = ssh_pki_import_privkey_base64(pem_str.c_str(), pass_ptr, nullptr, nullptr, &pkey);
        if (rc != SSH_OK || !pkey) {
            return std::unexpected(Error("ssh.Session.auth_publickey_mem: failed to parse private key from memory"));
        }
        rc = ssh_userauth_publickey(state_->raw_session, nullptr, pkey);
        ssh_key_free(pkey);
        if (rc != SSH_AUTH_SUCCESS) {
            return std::unexpected(Error(std::format("ssh.Session.auth_publickey_mem: authentication rejected ({})", ssh_get_error(state_->raw_session))));
        }
        state_->authenticated = true;
        return {};
#else
        (void)passphrase;
        return std::unexpected(Error("ssh.Session.auth_publickey_mem: libssh not compiled in"));
#endif
    }

    [[nodiscard]] auto auth_publickey_file(std::string_view private_key_path, std::string_view passphrase = "") -> std::expected<void, Error> {
        if (!state_) return std::unexpected(Error("ssh.Session.auth_publickey_file: invalid session"));
        std::lock_guard<std::mutex> lock(state_->mtx);
        if (!state_->connected) return std::unexpected(Error("ssh.Session.auth_publickey_file: not connected"));

        if (state_->is_mock) {
            if (private_key_path.empty()) return std::unexpected(Error("ssh.Session.auth_publickey_file: empty path"));
            state_->authenticated = true;
            return {};
        }

#if CFIRE_LIBSSH_ACTIVE
        ssh_key pkey = nullptr;
        std::string path_str(private_key_path);
        std::string pass_str(passphrase);
        const char* pass_ptr = passphrase.empty() ? nullptr : pass_str.c_str();
        int rc = ssh_pki_import_privkey_file(path_str.c_str(), pass_ptr, nullptr, nullptr, &pkey);
        if (rc != SSH_OK || !pkey) {
            return std::unexpected(Error(std::format("ssh.Session.auth_publickey_file: failed to load key '{}'", private_key_path)));
        }
        rc = ssh_userauth_publickey(state_->raw_session, nullptr, pkey);
        ssh_key_free(pkey);
        if (rc != SSH_AUTH_SUCCESS) {
            return std::unexpected(Error(std::format("ssh.Session.auth_publickey_file: authentication rejected ({})", ssh_get_error(state_->raw_session))));
        }
        state_->authenticated = true;
        return {};
#else
        (void)passphrase;
        return std::unexpected(Error("ssh.Session.auth_publickey_file: libssh not compiled in"));
#endif
    }

    [[nodiscard]] auto auth_password(std::string_view password) -> std::expected<void, Error> {
        if (!state_) return std::unexpected(Error("ssh.Session.auth_password: invalid session"));
        std::lock_guard<std::mutex> lock(state_->mtx);
        if (!state_->connected) return std::unexpected(Error("ssh.Session.auth_password: not connected"));
        if (password.empty()) return std::unexpected(Error("ssh.Session.auth_password: empty password"));

        if (state_->is_mock) {
            state_->authenticated = true;
            return {};
        }

#if CFIRE_LIBSSH_ACTIVE
        std::string pw(password);
        int rc = ssh_userauth_password(state_->raw_session, nullptr, pw.c_str());
        if (rc != SSH_AUTH_SUCCESS) {
            return std::unexpected(Error(std::format("ssh.Session.auth_password: authentication rejected ({})", ssh_get_error(state_->raw_session))));
        }
        state_->authenticated = true;
        return {};
#else
        return std::unexpected(Error("ssh.Session.auth_password: libssh not compiled in"));
#endif
    }

    [[nodiscard]] auto auth_agent() -> std::expected<void, Error> {
        if (!state_) return std::unexpected(Error("ssh.Session.auth_agent: invalid session"));
        std::lock_guard<std::mutex> lock(state_->mtx);
        if (!state_->connected) return std::unexpected(Error("ssh.Session.auth_agent: not connected"));

        if (state_->is_mock) {
            state_->authenticated = true;
            return {};
        }

#if CFIRE_LIBSSH_ACTIVE
        int rc = ssh_userauth_agent(state_->raw_session, nullptr);
        if (rc != SSH_AUTH_SUCCESS) {
            return std::unexpected(Error(std::format("ssh.Session.auth_agent: authentication failed ({})", ssh_get_error(state_->raw_session))));
        }
        state_->authenticated = true;
        return {};
#else
        return std::unexpected(Error("ssh.Session.auth_agent: libssh not compiled in"));
#endif
    }

    [[nodiscard]] auto exec(std::string_view command) -> std::expected<ExecResult, Error> {
        if (!state_) return std::unexpected(Error("ssh.Session.exec: invalid session"));
        std::lock_guard<std::mutex> lock(state_->mtx);
        if (!state_->connected || !state_->authenticated) {
            return std::unexpected(Error("ssh.Session.exec: session not authenticated"));
        }

        if (state_->is_mock) {
            ExecResult res;
            res.exit_code = 0;
            res.stdout_cf_ = std::format("mock-exec[{}@{}]: {}\n", state_->user, state_->host, command);
            return res;
        }

#if CFIRE_LIBSSH_ACTIVE
        ssh_channel ch = ssh_channel_new(state_->raw_session);
        if (!ch) return std::unexpected(Error("ssh.Session.exec: ssh_channel_new failed"));
        if (ssh_channel_open_session(ch) != SSH_OK) {
            ssh_channel_free(ch);
            return std::unexpected(Error("ssh.Session.exec: ssh_channel_open_session failed"));
        }
        std::string cmd_str(command);
        if (ssh_channel_request_exec(ch, cmd_str.c_str()) != SSH_OK) {
            ssh_channel_close(ch);
            ssh_channel_free(ch);
            return std::unexpected(Error("ssh.Session.exec: ssh_channel_request_exec failed"));
        }

        ExecResult res;
        char buf[4096];
        int nbytes = 0;
        while ((nbytes = ssh_channel_read(ch, buf, sizeof(buf), 0)) > 0) {
            res.stdout_cf_.append(buf, static_cast<std::size_t>(nbytes));
        }
        while ((nbytes = ssh_channel_read(ch, buf, sizeof(buf), 1)) > 0) {
            res.stderr_cf_.append(buf, static_cast<std::size_t>(nbytes));
        }
        res.exit_code = ssh_channel_get_exit_status(ch);
        ssh_channel_send_eof(ch);
        ssh_channel_close(ch);
        ssh_channel_free(ch);
        return res;
#else
        return std::unexpected(Error("ssh.Session.exec: libssh not compiled in"));
#endif
    }

    [[nodiscard]] auto open_pty(
        std::string_view term = "xterm-256color",
        std::int32_t cols = 80,
        std::int32_t rows = 24
    ) -> std::expected<Channel, Error> {
        if (!state_) return std::unexpected(Error("ssh.Session.open_pty: invalid session"));
        std::lock_guard<std::mutex> lock(state_->mtx);
        if (!state_->connected || !state_->authenticated) {
            return std::unexpected(Error("ssh.Session.open_pty: session not authenticated"));
        }

        auto ch_state = std::make_shared<detail::ChannelState>();
        ch_state->term = std::string(term);
        ch_state->cols = cols;
        ch_state->rows = rows;
        ch_state->is_mock = state_->is_mock;

        if (state_->is_mock) {
            ch_state->read_buffer = std::format("{}@{}:~$ ", state_->user, state_->host);
            return Channel(std::move(ch_state));
        }

#if CFIRE_LIBSSH_ACTIVE
        ssh_channel ch = ssh_channel_new(state_->raw_session);
        if (!ch) return std::unexpected(Error("ssh.Session.open_pty: ssh_channel_new failed"));
        if (ssh_channel_open_session(ch) != SSH_OK) {
            ssh_channel_free(ch);
            return std::unexpected(Error("ssh.Session.open_pty: ssh_channel_open_session failed"));
        }
        std::string term_str(term);
        if (ssh_channel_request_pty_size(ch, term_str.c_str(), cols, rows) != SSH_OK) {
            ssh_channel_close(ch);
            ssh_channel_free(ch);
            return std::unexpected(Error("ssh.Session.open_pty: ssh_channel_request_pty_size failed"));
        }
        if (ssh_channel_request_shell(ch) != SSH_OK) {
            ssh_channel_close(ch);
            ssh_channel_free(ch);
            return std::unexpected(Error("ssh.Session.open_pty: ssh_channel_request_shell failed"));
        }
        ch_state->raw_channel = ch;
        return Channel(std::move(ch_state));
#else
        return std::unexpected(Error("ssh.Session.open_pty: libssh not compiled in"));
#endif
    }

    [[nodiscard]] auto open_forward(
        std::string_view remote_host,
        std::uint16_t remote_port,
        std::string_view source_host = "127.0.0.1",
        std::uint16_t local_port = 0
    ) -> std::expected<Channel, Error> {
        if (!state_) return std::unexpected(Error("ssh.Session.open_forward: invalid session"));
        std::lock_guard<std::mutex> lock(state_->mtx);
        if (!state_->connected || !state_->authenticated) {
            return std::unexpected(Error("ssh.Session.open_forward: session not authenticated"));
        }

        auto ch_state = std::make_shared<detail::ChannelState>();
        ch_state->is_mock = state_->is_mock;
        if (state_->is_mock) {
            return Channel(std::move(ch_state));
        }

#if CFIRE_LIBSSH_ACTIVE
        ssh_channel ch = ssh_channel_new(state_->raw_session);
        if (!ch) return std::unexpected(Error("ssh.Session.open_forward: ssh_channel_new failed"));
        std::string rhost(remote_host);
        std::string shost(source_host);
        if (ssh_channel_open_forward(ch, rhost.c_str(), remote_port, shost.c_str(), local_port) != SSH_OK) {
            ssh_channel_free(ch);
            return std::unexpected(Error("ssh.Session.open_forward: ssh_channel_open_forward failed"));
        }
        ch_state->raw_channel = ch;
        return Channel(std::move(ch_state));
#else
        (void)remote_host;
        (void)remote_port;
        (void)source_host;
        (void)local_port;
        return std::unexpected(Error("ssh.Session.open_forward: libssh not compiled in"));
#endif
    }

    [[nodiscard]] auto sftp_write_text(std::string_view remote_path, std::string_view data, std::uint32_t mode = 0644) -> std::expected<void, Error> {
        if (!state_) return std::unexpected(Error("ssh.Session.sftp_write_text: invalid session"));
        std::lock_guard<std::mutex> lock(state_->mtx);
        if (!state_->connected || !state_->authenticated) {
            return std::unexpected(Error("ssh.Session.sftp_write_text: session not authenticated"));
        }

        if (state_->is_mock) {
            auto& srv = detail::mock_server();
            std::lock_guard<std::mutex> slock(srv.mtx);
            srv.sftp_files[std::string(remote_path)] = std::string(data);
            srv.sftp_modes[std::string(remote_path)] = mode;
            return {};
        }

#if CFIRE_LIBSSH_ACTIVE
        if (!ensure_sftp_locked()) return std::unexpected(Error("ssh.Session.sftp_write_text: failed to initialize SFTP subsystem"));
        std::string rpath(remote_path);
        sftp_file f = sftp_open(state_->raw_sftp, rpath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, static_cast<mode_t>(mode));
        if (!f) return std::unexpected(Error(std::format("ssh.Session.sftp_write_text: failed to open '{}'", remote_path)));
        ssize_t n = sftp_write(f, data.data(), data.size());
        sftp_close(f);
        if (n < 0 || static_cast<std::size_t>(n) != data.size()) {
            return std::unexpected(Error("ssh.Session.sftp_write_text: incomplete SFTP write"));
        }
        return {};
#else
        return std::unexpected(Error("ssh.Session.sftp_write_text: libssh not compiled in"));
#endif
    }

    [[nodiscard]] auto sftp_write_bytes(std::string_view remote_path, std::span<const std::byte> data, std::uint32_t mode = 0644) -> std::expected<void, Error> {
        return sftp_write_text(remote_path, std::string_view(reinterpret_cast<const char*>(data.data()), data.size()), mode);
    }

    [[nodiscard]] auto sftp_write_bytes(std::string_view remote_path, std::span<const std::uint8_t> data, std::uint32_t mode = 0644) -> std::expected<void, Error> {
        return sftp_write_text(remote_path, std::string_view(reinterpret_cast<const char*>(data.data()), data.size()), mode);
    }

    [[nodiscard]] auto sftp_read_text(std::string_view remote_path) -> std::expected<std::string, Error> {
        if (!state_) return std::unexpected(Error("ssh.Session.sftp_read_text: invalid session"));
        std::lock_guard<std::mutex> lock(state_->mtx);
        if (!state_->connected || !state_->authenticated) {
            return std::unexpected(Error("ssh.Session.sftp_read_text: session not authenticated"));
        }

        if (state_->is_mock) {
            auto& srv = detail::mock_server();
            std::lock_guard<std::mutex> slock(srv.mtx);
            auto it = srv.sftp_files.find(std::string(remote_path));
            if (it == srv.sftp_files.end()) {
                return std::unexpected(Error(std::format("ssh.Session.sftp_read_text: remote file '{}' not found", remote_path)));
            }
            return it->second;
        }

#if CFIRE_LIBSSH_ACTIVE
        if (!ensure_sftp_locked()) return std::unexpected(Error("ssh.Session.sftp_read_text: failed to initialize SFTP subsystem"));
        std::string rpath(remote_path);
        sftp_file f = sftp_open(state_->raw_sftp, rpath.c_str(), O_RDONLY, 0);
        if (!f) return std::unexpected(Error(std::format("ssh.Session.sftp_read_text: failed to open '{}'", remote_path)));
        std::string out;
        char buf[8192];
        ssize_t n = 0;
        while ((n = sftp_read(f, buf, sizeof(buf))) > 0) {
            out.append(buf, static_cast<std::size_t>(n));
        }
        sftp_close(f);
        if (n < 0) return std::unexpected(Error("ssh.Session.sftp_read_text: read error"));
        return out;
#else
        return std::unexpected(Error("ssh.Session.sftp_read_text: libssh not compiled in"));
#endif
    }

    [[nodiscard]] auto sftp_read_bytes(std::string_view remote_path) -> std::expected<std::vector<std::byte>, Error> {
        auto s = sftp_read_text(remote_path);
        if (!s) return std::unexpected(s.error());
        auto sp = cfire::to_bytes(*s);
        return std::vector<std::byte>(sp.begin(), sp.end());
    }

    [[nodiscard]] auto sftp_list_dir(std::string_view remote_path) -> std::expected<std::vector<SftpEntry>, Error> {
        if (!state_) return std::unexpected(Error("ssh.Session.sftp_list_dir: invalid session"));
        std::lock_guard<std::mutex> lock(state_->mtx);
        if (!state_->connected || !state_->authenticated) {
            return std::unexpected(Error("ssh.Session.sftp_list_dir: session not authenticated"));
        }

        if (state_->is_mock) {
            auto& srv = detail::mock_server();
            std::lock_guard<std::mutex> slock(srv.mtx);
            std::vector<SftpEntry> entries;
            std::string prefix(remote_path);
            if (!prefix.ends_with("/")) prefix += "/";
            for (const auto& [k, v] : srv.sftp_files) {
                if (k.starts_with(prefix)) {
                    std::string rel = k.substr(prefix.size());
                    if (!rel.empty() && rel.find('/') == std::string::npos) {
                        SftpEntry e;
                        e.name = rel;
                        e.size = v.size();
                        e.is_dir = false;
                        e.permissions = srv.sftp_modes.contains(k) ? srv.sftp_modes[k] : 0644;
                        entries.push_back(std::move(e));
                    }
                }
            }
            return entries;
        }

#if CFIRE_LIBSSH_ACTIVE
        if (!ensure_sftp_locked()) return std::unexpected(Error("ssh.Session.sftp_list_dir: failed to initialize SFTP subsystem"));
        std::string rpath(remote_path);
        sftp_dir dir = sftp_opendir(state_->raw_sftp, rpath.c_str());
        if (!dir) return std::unexpected(Error(std::format("ssh.Session.sftp_list_dir: failed to open dir '{}'", remote_path)));
        std::vector<SftpEntry> entries;
        while (sftp_attributes attr = sftp_readdir(state_->raw_sftp, dir)) {
            std::string name = attr->name ? attr->name : "";
            if (name != "." && name != "..") {
                SftpEntry e;
                e.name = std::move(name);
                e.size = attr->size;
                e.is_dir = (attr->type == SSH_FILEXFER_TYPE_DIRECTORY);
                e.permissions = attr->permissions;
                entries.push_back(std::move(e));
            }
            sftp_attributes_free(attr);
        }
        sftp_closedir(dir);
        return entries;
#else
        return std::unexpected(Error("ssh.Session.sftp_list_dir: libssh not compiled in"));
#endif
    }

    [[nodiscard]] auto sftp_mkdir(std::string_view remote_path, std::uint32_t mode = 0755) -> std::expected<void, Error> {
        if (!state_) return std::unexpected(Error("ssh.Session.sftp_mkdir: invalid session"));
        std::lock_guard<std::mutex> lock(state_->mtx);
        if (!state_->connected || !state_->authenticated) {
            return std::unexpected(Error("ssh.Session.sftp_mkdir: session not authenticated"));
        }
        if (state_->is_mock) return {};
#if CFIRE_LIBSSH_ACTIVE
        if (!ensure_sftp_locked()) return std::unexpected(Error("ssh.Session.sftp_mkdir: failed to initialize SFTP subsystem"));
        std::string rpath(remote_path);
        if (sftp_mkdir(state_->raw_sftp, rpath.c_str(), static_cast<mode_t>(mode)) != SSH_OK) {
            return std::unexpected(Error(std::format("ssh.Session.sftp_mkdir: failed to create '{}'", remote_path)));
        }
        return {};
#else
        (void)remote_path;
        (void)mode;
        return std::unexpected(Error("ssh.Session.sftp_mkdir: libssh not compiled in"));
#endif
    }

    [[nodiscard]] auto sftp_remove(std::string_view remote_path) -> std::expected<void, Error> {
        if (!state_) return std::unexpected(Error("ssh.Session.sftp_remove: invalid session"));
        std::lock_guard<std::mutex> lock(state_->mtx);
        if (!state_->connected || !state_->authenticated) {
            return std::unexpected(Error("ssh.Session.sftp_remove: session not authenticated"));
        }
        if (state_->is_mock) {
            auto& srv = detail::mock_server();
            std::lock_guard<std::mutex> slock(srv.mtx);
            srv.sftp_files.erase(std::string(remote_path));
            srv.sftp_modes.erase(std::string(remote_path));
            return {};
        }
#if CFIRE_LIBSSH_ACTIVE
        if (!ensure_sftp_locked()) return std::unexpected(Error("ssh.Session.sftp_remove: failed to initialize SFTP subsystem"));
        std::string rpath(remote_path);
        if (sftp_unlink(state_->raw_sftp, rpath.c_str()) != SSH_OK) {
            return std::unexpected(Error(std::format("ssh.Session.sftp_remove: failed to remove '{}'", remote_path)));
        }
        return {};
#else
        return std::unexpected(Error("ssh.Session.sftp_remove: libssh not compiled in"));
#endif
    }

    void close() {
        if (state_) state_->close();
    }

private:
#if CFIRE_LIBSSH_ACTIVE
    auto ensure_sftp_locked() -> bool {
        if (state_->raw_sftp) return true;
        if (!state_->raw_session) return false;
        state_->raw_sftp = sftp_new(state_->raw_session);
        if (!state_->raw_sftp) return false;
        if (sftp_init(state_->raw_sftp) != SSH_OK) {
            sftp_free(state_->raw_sftp);
            state_->raw_sftp = nullptr;
            return false;
        }
        return true;
    }
#endif
    std::shared_ptr<detail::ChannelState> dummy_;
    std::shared_ptr<detail::SessionState> state_;
};

[[nodiscard]] inline auto dial(
    std::string_view host,
    std::uint16_t port = 22,
    std::string_view user = "root",
    std::int64_t timeout_ms = 10000
) -> std::expected<Session, Error> {
    if (host.empty()) {
        return std::unexpected(Error("ssh.dial: empty host"));
    }

    auto st = std::make_shared<detail::SessionState>();
    st->host = std::string(host);
    st->port = port;
    st->user = user.empty() ? "root" : std::string(user);
    st->is_mock = detail::is_mock_mode();

    if (st->is_mock) {
        st->connected = true;
        auto digest = cfire::sha256::hash_bytes(std::format("cfire-mock-hostkey:{}:{}", host, port));
        st->host_fingerprint = "SHA256:" + cfire::base64::encode(digest);
        return Session(std::move(st));
    }

#if CFIRE_LIBSSH_ACTIVE
    ssh_session sess = ssh_new();
    if (!sess) return std::unexpected(Error("ssh.dial: ssh_new failed"));

    int port_int = static_cast<int>(port);
    long timeout_sec = static_cast<long>(std::max<std::int64_t>(1, (timeout_ms + 999) / 1000));
    ssh_options_set(sess, SSH_OPTIONS_HOST, st->host.c_str());
    ssh_options_set(sess, SSH_OPTIONS_PORT, &port_int);
    ssh_options_set(sess, SSH_OPTIONS_USER, st->user.c_str());
    ssh_options_set(sess, SSH_OPTIONS_TIMEOUT, &timeout_sec);

    if (ssh_connect(sess) != SSH_OK) {
        std::string err = ssh_get_error(sess);
        ssh_free(sess);
        return std::unexpected(Error(std::format("ssh.dial({}:{}): {}", host, port, err)));
    }

    ssh_key srv_key = nullptr;
    if (ssh_get_server_publickey(sess, &srv_key) == SSH_OK && srv_key) {
        unsigned char* hash = nullptr;
        std::size_t hlen = 0;
        if (ssh_get_publickey_hash(srv_key, SSH_PUBLICKEY_HASH_SHA256, &hash, &hlen) == SSH_OK && hash) {
            st->host_fingerprint = "SHA256:" + cfire::base64::encode(
                std::span<const std::byte>(reinterpret_cast<const std::byte*>(hash), hlen));
            ssh_clean_pubkey_hash(&hash);
        }
        ssh_key_free(srv_key);
    }

    st->raw_session = sess;
    st->connected = true;
    return Session(std::move(st));
#else
    (void)timeout_ms;
    return std::unexpected(Error("ssh.dial: libssh is not installed on this system"));
#endif
}

} // namespace cfire::ssh

namespace cfire::core {
namespace ssh = ::cfire::ssh;
} // namespace cfire::core

namespace ssh = ::cfire::ssh;
