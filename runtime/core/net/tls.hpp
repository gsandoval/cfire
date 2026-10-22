#pragma once
#include "tcp.hpp"

#include <dlfcn.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <expected>
#include <cstring>

// --- `core:net/tls` (TLS 1.2 / 1.3 & X.509 Certificate Engine via Dynamic OpenSSL Bridge) ---
namespace cfire::tls {

namespace detail {

struct OpenSslApi {
    void* crypto_handle = nullptr;
    void* ssl_handle = nullptr;
    bool loaded = false;

    // libssl functions
    const void* (*TLS_client_method)() = nullptr;
    const void* (*TLS_server_method)() = nullptr;
    void* (*SSL_CTX_new)(const void* method) = nullptr;
    void (*SSL_CTX_free)(void* ctx) = nullptr;
    void (*SSL_CTX_set_verify)(void* ctx, int mode, void* cb) = nullptr;
    int (*SSL_CTX_set_default_verify_paths)(void* ctx) = nullptr;
    int (*SSL_CTX_load_verify_locations)(void* ctx, const char* cafile, const char* capath) = nullptr;
    int (*SSL_CTX_use_certificate_file)(void* ctx, const char* file, int type) = nullptr;
    int (*SSL_CTX_use_certificate_chain_file)(void* ctx, const char* file) = nullptr;
    int (*SSL_CTX_use_PrivateKey_file)(void* ctx, const char* file, int type) = nullptr;
    int (*SSL_CTX_check_private_key)(const void* ctx) = nullptr;
    int (*SSL_CTX_use_certificate)(void* ctx, void* x509) = nullptr;
    int (*SSL_CTX_use_PrivateKey)(void* ctx, void* pkey) = nullptr;
    void* (*SSL_new)(void* ctx) = nullptr;
    void (*SSL_free)(void* ssl) = nullptr;
    int (*SSL_set_fd)(void* ssl, int fd) = nullptr;
    long (*SSL_ctrl)(void* ssl, int cmd, long larg, void* parg) = nullptr;
    int (*SSL_set1_host)(void* ssl, const char* hostname) = nullptr;
    int (*SSL_connect)(void* ssl) = nullptr;
    int (*SSL_accept)(void* ssl) = nullptr;
    int (*SSL_read)(void* ssl, void* buf, int num) = nullptr;
    int (*SSL_write)(void* ssl, const void* buf, int num) = nullptr;
    int (*SSL_shutdown)(void* ssl) = nullptr;
    long (*SSL_get_verify_result)(const void* ssl) = nullptr;

    // libcrypto functions
    unsigned long (*ERR_get_error)() = nullptr;
    void (*ERR_error_string_n)(unsigned long e, char* buf, std::size_t len) = nullptr;
    const void* (*BIO_s_mem)() = nullptr;
    void* (*BIO_new)(const void* method) = nullptr;
    void* (*BIO_new_mem_buf)(const void* buf, int len) = nullptr;
    int (*BIO_free)(void* bio) = nullptr;
    long (*BIO_ctrl)(void* bio, int cmd, long larg, void* parg) = nullptr;
    void* (*PEM_read_bio_X509)(void* bio, void** x, void* cb, void* u) = nullptr;
    void* (*PEM_read_bio_PrivateKey)(void* bio, void** x, void* cb, void* u) = nullptr;
    int (*PEM_write_bio_X509)(void* bio, void* x) = nullptr;
    int (*PEM_write_bio_PrivateKey)(void* bio, void* pkey, const void* enc, unsigned char* kstr, int klen, void* cb, void* u) = nullptr;
    void* (*X509_new)() = nullptr;
    void (*X509_free)(void* x) = nullptr;
    int (*X509_set_version)(void* x, long version) = nullptr;
    void* (*X509_get_serialNumber)(void* x) = nullptr;
    int (*ASN1_INTEGER_set)(void* a, long v) = nullptr;
    void* (*X509_gmtime_adj)(void* s, long adj) = nullptr;
    void* (*X509_getm_notBefore)(const void* x) = nullptr;
    void* (*X509_getm_notAfter)(const void* x) = nullptr;
    int (*X509_set_pubkey)(void* x, void* pkey) = nullptr;
    void* (*X509_get_subject_name)(const void* a) = nullptr;
    int (*X509_NAME_add_entry_by_txt)(void* name, const char* field, int type, const unsigned char* bytes, int len, int loc, int set) = nullptr;
    int (*X509_set_issuer_name)(void* x, const void* name) = nullptr;
    int (*X509_sign)(void* x, void* pkey, const void* md) = nullptr;
    const void* (*EVP_sha256)() = nullptr;
    void* (*EVP_PKEY_CTX_new_id)(int id, void* e) = nullptr;
    void (*EVP_PKEY_CTX_free)(void* ctx) = nullptr;
    int (*EVP_PKEY_keygen_init)(void* ctx) = nullptr;
    int (*EVP_PKEY_CTX_set_rsa_keygen_bits)(void* ctx, int bits) = nullptr;
    int (*EVP_PKEY_keygen)(void* ctx, void** ppkey) = nullptr;
    void (*EVP_PKEY_free)(void* pkey) = nullptr;
};

template <typename FnPtr>
inline void load_sym(void* handle, const char* name, FnPtr& out) noexcept {
    if (!handle) return;
    void* sym = ::dlsym(handle, name);
    if (sym) {
        std::memcpy(&out, &sym, sizeof(FnPtr));
    }
}

inline auto api() -> const OpenSslApi& {
    static OpenSslApi instance = []() {
        OpenSslApi a;
        static constexpr const char* kCryptoPaths[] = {
            "/opt/homebrew/opt/openssl@3/lib/libcrypto.dylib",
            "/opt/homebrew/lib/libcrypto.dylib",
            "/usr/local/opt/openssl@3/lib/libcrypto.dylib",
            "/usr/local/opt/openssl/lib/libcrypto.dylib",
            "/usr/local/lib/libcrypto.dylib",
            "libcrypto.3.dylib",
            "libcrypto.dylib",
            "libcrypto.so.3",
            "libcrypto.so",
        };
        static constexpr const char* kSslPaths[] = {
            "/opt/homebrew/opt/openssl@3/lib/libssl.dylib",
            "/opt/homebrew/lib/libssl.dylib",
            "/usr/local/opt/openssl@3/lib/libssl.dylib",
            "/usr/local/opt/openssl/lib/libssl.dylib",
            "/usr/local/lib/libssl.dylib",
            "libssl.3.dylib",
            "libssl.dylib",
            "libssl.so.3",
            "libssl.so",
        };

        for (const char* p : kCryptoPaths) {
            a.crypto_handle = ::dlopen(p, RTLD_LAZY | RTLD_GLOBAL);
            if (a.crypto_handle) break;
        }
        for (const char* p : kSslPaths) {
            a.ssl_handle = ::dlopen(p, RTLD_LAZY | RTLD_GLOBAL);
            if (a.ssl_handle) break;
        }

        if (!a.crypto_handle || !a.ssl_handle) {
            return a;
        }

        load_sym(a.ssl_handle, "TLS_client_method", a.TLS_client_method);
        if (!a.TLS_client_method) load_sym(a.ssl_handle, "SSLv23_client_method", a.TLS_client_method);
        load_sym(a.ssl_handle, "TLS_server_method", a.TLS_server_method);
        if (!a.TLS_server_method) load_sym(a.ssl_handle, "SSLv23_server_method", a.TLS_server_method);

        load_sym(a.ssl_handle, "SSL_CTX_new", a.SSL_CTX_new);
        load_sym(a.ssl_handle, "SSL_CTX_free", a.SSL_CTX_free);
        load_sym(a.ssl_handle, "SSL_CTX_set_verify", a.SSL_CTX_set_verify);
        load_sym(a.ssl_handle, "SSL_CTX_set_default_verify_paths", a.SSL_CTX_set_default_verify_paths);
        load_sym(a.ssl_handle, "SSL_CTX_load_verify_locations", a.SSL_CTX_load_verify_locations);
        load_sym(a.ssl_handle, "SSL_CTX_use_certificate_file", a.SSL_CTX_use_certificate_file);
        load_sym(a.ssl_handle, "SSL_CTX_use_certificate_chain_file", a.SSL_CTX_use_certificate_chain_file);
        load_sym(a.ssl_handle, "SSL_CTX_use_PrivateKey_file", a.SSL_CTX_use_PrivateKey_file);
        load_sym(a.ssl_handle, "SSL_CTX_check_private_key", a.SSL_CTX_check_private_key);
        load_sym(a.ssl_handle, "SSL_CTX_use_certificate", a.SSL_CTX_use_certificate);
        load_sym(a.ssl_handle, "SSL_CTX_use_PrivateKey", a.SSL_CTX_use_PrivateKey);
        load_sym(a.ssl_handle, "SSL_new", a.SSL_new);
        load_sym(a.ssl_handle, "SSL_free", a.SSL_free);
        load_sym(a.ssl_handle, "SSL_set_fd", a.SSL_set_fd);
        load_sym(a.ssl_handle, "SSL_ctrl", a.SSL_ctrl);
        load_sym(a.ssl_handle, "SSL_set1_host", a.SSL_set1_host);
        load_sym(a.ssl_handle, "SSL_connect", a.SSL_connect);
        load_sym(a.ssl_handle, "SSL_accept", a.SSL_accept);
        load_sym(a.ssl_handle, "SSL_read", a.SSL_read);
        load_sym(a.ssl_handle, "SSL_write", a.SSL_write);
        load_sym(a.ssl_handle, "SSL_shutdown", a.SSL_shutdown);
        load_sym(a.ssl_handle, "SSL_get_verify_result", a.SSL_get_verify_result);

        load_sym(a.crypto_handle, "ERR_get_error", a.ERR_get_error);
        load_sym(a.crypto_handle, "ERR_error_string_n", a.ERR_error_string_n);
        load_sym(a.crypto_handle, "BIO_s_mem", a.BIO_s_mem);
        load_sym(a.crypto_handle, "BIO_new", a.BIO_new);
        load_sym(a.crypto_handle, "BIO_new_mem_buf", a.BIO_new_mem_buf);
        load_sym(a.crypto_handle, "BIO_free", a.BIO_free);
        load_sym(a.crypto_handle, "BIO_ctrl", a.BIO_ctrl);
        load_sym(a.crypto_handle, "PEM_read_bio_X509", a.PEM_read_bio_X509);
        load_sym(a.crypto_handle, "PEM_read_bio_PrivateKey", a.PEM_read_bio_PrivateKey);
        load_sym(a.crypto_handle, "PEM_write_bio_X509", a.PEM_write_bio_X509);
        load_sym(a.crypto_handle, "PEM_write_bio_PrivateKey", a.PEM_write_bio_PrivateKey);
        load_sym(a.crypto_handle, "X509_new", a.X509_new);
        load_sym(a.crypto_handle, "X509_free", a.X509_free);
        load_sym(a.crypto_handle, "X509_set_version", a.X509_set_version);
        load_sym(a.crypto_handle, "X509_get_serialNumber", a.X509_get_serialNumber);
        load_sym(a.crypto_handle, "ASN1_INTEGER_set", a.ASN1_INTEGER_set);
        load_sym(a.crypto_handle, "X509_gmtime_adj", a.X509_gmtime_adj);
        load_sym(a.crypto_handle, "X509_getm_notBefore", a.X509_getm_notBefore);
        load_sym(a.crypto_handle, "X509_getm_notAfter", a.X509_getm_notAfter);
        load_sym(a.crypto_handle, "X509_set_pubkey", a.X509_set_pubkey);
        load_sym(a.crypto_handle, "X509_get_subject_name", a.X509_get_subject_name);
        load_sym(a.crypto_handle, "X509_NAME_add_entry_by_txt", a.X509_NAME_add_entry_by_txt);
        load_sym(a.crypto_handle, "X509_set_issuer_name", a.X509_set_issuer_name);
        load_sym(a.crypto_handle, "X509_sign", a.X509_sign);
        load_sym(a.crypto_handle, "EVP_sha256", a.EVP_sha256);
        load_sym(a.crypto_handle, "EVP_PKEY_CTX_new_id", a.EVP_PKEY_CTX_new_id);
        load_sym(a.crypto_handle, "EVP_PKEY_CTX_free", a.EVP_PKEY_CTX_free);
        load_sym(a.crypto_handle, "EVP_PKEY_keygen_init", a.EVP_PKEY_keygen_init);
        load_sym(a.crypto_handle, "EVP_PKEY_CTX_set_rsa_keygen_bits", a.EVP_PKEY_CTX_set_rsa_keygen_bits);
        load_sym(a.crypto_handle, "EVP_PKEY_keygen", a.EVP_PKEY_keygen);
        load_sym(a.crypto_handle, "EVP_PKEY_free", a.EVP_PKEY_free);

        a.loaded = (a.TLS_client_method && a.TLS_server_method && a.SSL_CTX_new && a.SSL_CTX_free &&
                    a.SSL_new && a.SSL_free && a.SSL_set_fd && a.SSL_connect && a.SSL_accept &&
                    a.SSL_read && a.SSL_write && a.SSL_shutdown);
        return a;
    }();
    return instance;
}

inline auto last_openssl_error() -> std::string {
    const auto& a = api();
    if (!a.ERR_get_error || !a.ERR_error_string_n) return "OpenSSL error";
    unsigned long code = a.ERR_get_error();
    if (code == 0) return "handshake or I/O error";
    char buf[256]{};
    a.ERR_error_string_n(code, buf, sizeof(buf));
    return std::string(buf);
}

inline auto tls_read_bridge(void* ssl, void* buf, std::size_t len) -> ssize_t {
    const auto& a = api();
    if (!ssl || !a.SSL_read) return -1;
    int max_chunk = (len > 16384) ? 16384 : static_cast<int>(len);
    int n = a.SSL_read(ssl, buf, max_chunk);
    return static_cast<ssize_t>(n);
}

inline auto tls_write_bridge(void* ssl, const void* buf, std::size_t len) -> ssize_t {
    const auto& a = api();
    if (!ssl || !a.SSL_write) return -1;
    int max_chunk = (len > 16384) ? 16384 : static_cast<int>(len);
    int n = a.SSL_write(ssl, buf, max_chunk);
    return static_cast<ssize_t>(n);
}

inline void tls_close_owned_ctx(void* ssl, void* owned_ctx) {
    const auto& a = api();
    if (ssl) {
        if (a.SSL_shutdown) (void)a.SSL_shutdown(ssl);
        if (a.SSL_free) a.SSL_free(ssl);
    }
    if (owned_ctx && a.SSL_CTX_free) {
        a.SSL_CTX_free(owned_ctx);
    }
}

inline void tls_close_shared_ctx(void* ssl, void* shared_holder) {
    const auto& a = api();
    if (ssl) {
        if (a.SSL_shutdown) (void)a.SSL_shutdown(ssl);
        if (a.SSL_free) a.SSL_free(ssl);
    }
    if (shared_holder) {
        delete static_cast<std::shared_ptr<void>*>(shared_holder);
    }
}

} // namespace detail

[[nodiscard]] inline auto available() noexcept -> bool {
    return detail::api().loaded;
}

struct CertKeyPair {
    std::string cert_pem{};
    std::string key_pem{};
};

[[nodiscard]] inline auto generate_self_signed_pem(
    std::string_view common_name = "localhost",
    int days = 365
) -> std::expected<CertKeyPair, cfire::Error> {
    const auto& a = detail::api();
    if (!a.loaded || !a.EVP_PKEY_CTX_new_id || !a.EVP_PKEY_keygen_init ||
        !a.EVP_PKEY_keygen || !a.X509_new || !a.X509_sign || !a.BIO_s_mem ||
        !a.BIO_new || !a.BIO_ctrl || !a.PEM_write_bio_X509 || !a.PEM_write_bio_PrivateKey) {
        return cfire::make_error("OpenSSL X.509 certificate generation symbols not available");
    }

    static constexpr int kEvpPkeyRsa = 6;
    static constexpr int kMbstringAsc = 0x1001;
    static constexpr int kBioCtrlInfo = 3;

    void* pctx = a.EVP_PKEY_CTX_new_id(kEvpPkeyRsa, nullptr);
    if (!pctx) return cfire::make_error("EVP_PKEY_CTX_new_id failed");

    if (a.EVP_PKEY_keygen_init(pctx) <= 0) {
        a.EVP_PKEY_CTX_free(pctx);
        return cfire::make_error("EVP_PKEY_keygen_init failed");
    }
    if (a.EVP_PKEY_CTX_set_rsa_keygen_bits) {
        (void)a.EVP_PKEY_CTX_set_rsa_keygen_bits(pctx, 2048);
    }

    void* pkey = nullptr;
    if (a.EVP_PKEY_keygen(pctx, &pkey) <= 0 || !pkey) {
        a.EVP_PKEY_CTX_free(pctx);
        return cfire::make_error("EVP_PKEY_keygen failed");
    }
    a.EVP_PKEY_CTX_free(pctx);

    void* x509 = a.X509_new();
    if (!x509) {
        a.EVP_PKEY_free(pkey);
        return cfire::make_error("X509_new failed");
    }

    if (a.X509_set_version) (void)a.X509_set_version(x509, 2); // X.509 v3
    if (a.X509_get_serialNumber && a.ASN1_INTEGER_set) {
        (void)a.ASN1_INTEGER_set(a.X509_get_serialNumber(x509), 1);
    }
    if (a.X509_gmtime_adj && a.X509_getm_notBefore && a.X509_getm_notAfter) {
        (void)a.X509_gmtime_adj(a.X509_getm_notBefore(x509), 0);
        (void)a.X509_gmtime_adj(a.X509_getm_notAfter(x509), static_cast<long>(days) * 86400L);
    }
    (void)a.X509_set_pubkey(x509, pkey);

    void* name = a.X509_get_subject_name(x509);
    if (name && a.X509_NAME_add_entry_by_txt && !common_name.empty()) {
        std::string cn(common_name);
        (void)a.X509_NAME_add_entry_by_txt(
            name, "CN", kMbstringAsc,
            reinterpret_cast<const unsigned char*>(cn.c_str()),
            static_cast<int>(cn.size()), -1, 0
        );
        if (a.X509_set_issuer_name) {
            (void)a.X509_set_issuer_name(x509, name);
        }
    }

    if (a.X509_sign(x509, pkey, a.EVP_sha256()) <= 0) {
        a.X509_free(x509);
        a.EVP_PKEY_free(pkey);
        return cfire::make_error("X509_sign failed");
    }

    CertKeyPair out;
    void* cert_bio = a.BIO_new(a.BIO_s_mem());
    void* key_bio = a.BIO_new(a.BIO_s_mem());
    if (!cert_bio || !key_bio) {
        if (cert_bio) a.BIO_free(cert_bio);
        if (key_bio) a.BIO_free(key_bio);
        a.X509_free(x509);
        a.EVP_PKEY_free(pkey);
        return cfire::make_error("BIO_new failed");
    }

    (void)a.PEM_write_bio_X509(cert_bio, x509);
    (void)a.PEM_write_bio_PrivateKey(key_bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);

    char* cert_ptr = nullptr;
    long cert_len = a.BIO_ctrl(cert_bio, kBioCtrlInfo, 0, &cert_ptr);
    if (cert_len > 0 && cert_ptr) {
        out.cert_pem.assign(cert_ptr, static_cast<std::size_t>(cert_len));
    }

    char* key_ptr = nullptr;
    long key_len = a.BIO_ctrl(key_bio, kBioCtrlInfo, 0, &key_ptr);
    if (key_len > 0 && key_ptr) {
        out.key_pem.assign(key_ptr, static_cast<std::size_t>(key_len));
    }

    a.BIO_free(cert_bio);
    a.BIO_free(key_bio);
    a.X509_free(x509);
    a.EVP_PKEY_free(pkey);

    if (out.cert_pem.empty() || out.key_pem.empty()) {
        return cfire::make_error("failed to serialize generated X.509 PEM certificate or private key");
    }
    return out;
}

class ServerContext {
public:
    ServerContext() = default;
    explicit ServerContext(std::shared_ptr<void> ctx) noexcept : ctx_(std::move(ctx)) {}

    [[nodiscard]] auto valid() const noexcept -> bool { return ctx_ != nullptr; }

    static auto from_files(std::string_view cert_file, std::string_view key_file)
        -> std::expected<ServerContext, cfire::Error>
    {
        const auto& a = detail::api();
        if (!a.loaded) return cfire::make_error("OpenSSL library (libssl/libcrypto) not available on system");

        void* raw_ctx = a.SSL_CTX_new(a.TLS_server_method());
        if (!raw_ctx) return cfire::make_error("SSL_CTX_new(TLS_server_method) failed");

        std::shared_ptr<void> ctx(raw_ctx, [](void* p) {
            if (p && detail::api().SSL_CTX_free) detail::api().SSL_CTX_free(p);
        });

        std::string cert_path(cert_file);
        std::string key_path(key_file);
        static constexpr int kSslFiletypePem = 1;

        int cert_ok = a.SSL_CTX_use_certificate_chain_file
            ? a.SSL_CTX_use_certificate_chain_file(ctx.get(), cert_path.c_str())
            : a.SSL_CTX_use_certificate_file(ctx.get(), cert_path.c_str(), kSslFiletypePem);
        if (cert_ok != 1) {
            return cfire::make_error(std::format("failed to load TLS certificate file '{}': {}", cert_file, detail::last_openssl_error()));
        }

        if (a.SSL_CTX_use_PrivateKey_file(ctx.get(), key_path.c_str(), kSslFiletypePem) != 1) {
            return cfire::make_error(std::format("failed to load TLS private key file '{}': {}", key_file, detail::last_openssl_error()));
        }

        if (a.SSL_CTX_check_private_key && a.SSL_CTX_check_private_key(ctx.get()) != 1) {
            return cfire::make_error("TLS private key does not match the certificate public key");
        }

        return ServerContext(std::move(ctx));
    }

    static auto from_pem(std::string_view cert_pem, std::string_view key_pem)
        -> std::expected<ServerContext, cfire::Error>
    {
        const auto& a = detail::api();
        if (!a.loaded || !a.BIO_new_mem_buf || !a.PEM_read_bio_X509 || !a.PEM_read_bio_PrivateKey ||
            !a.SSL_CTX_use_certificate || !a.SSL_CTX_use_PrivateKey) {
            return cfire::make_error("OpenSSL in-memory PEM loading symbols not available");
        }

        void* raw_ctx = a.SSL_CTX_new(a.TLS_server_method());
        if (!raw_ctx) return cfire::make_error("SSL_CTX_new(TLS_server_method) failed");

        std::shared_ptr<void> ctx(raw_ctx, [](void* p) {
            if (p && detail::api().SSL_CTX_free) detail::api().SSL_CTX_free(p);
        });

        void* cert_bio = a.BIO_new_mem_buf(cert_pem.data(), static_cast<int>(cert_pem.size()));
        if (!cert_bio) return cfire::make_error("BIO_new_mem_buf for certificate failed");
        void* x509 = a.PEM_read_bio_X509(cert_bio, nullptr, nullptr, nullptr);
        a.BIO_free(cert_bio);
        if (!x509) {
            return cfire::make_error(std::format("failed to parse PEM certificate: {}", detail::last_openssl_error()));
        }

        int use_cert = a.SSL_CTX_use_certificate(ctx.get(), x509);
        a.X509_free(x509);
        if (use_cert != 1) {
            return cfire::make_error(std::format("SSL_CTX_use_certificate failed: {}", detail::last_openssl_error()));
        }

        void* key_bio = a.BIO_new_mem_buf(key_pem.data(), static_cast<int>(key_pem.size()));
        if (!key_bio) return cfire::make_error("BIO_new_mem_buf for private key failed");
        void* pkey = a.PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr);
        a.BIO_free(key_bio);
        if (!pkey) {
            return cfire::make_error(std::format("failed to parse PEM private key: {}", detail::last_openssl_error()));
        }

        int use_key = a.SSL_CTX_use_PrivateKey(ctx.get(), pkey);
        a.EVP_PKEY_free(pkey);
        if (use_key != 1) {
            return cfire::make_error(std::format("SSL_CTX_use_PrivateKey failed: {}", detail::last_openssl_error()));
        }

        if (a.SSL_CTX_check_private_key && a.SSL_CTX_check_private_key(ctx.get()) != 1) {
            return cfire::make_error("TLS private key does not match the certificate public key");
        }

        return ServerContext(std::move(ctx));
    }

    auto wrap_server(cfire::net::TcpConn& conn) const -> std::expected<void, cfire::Error> {
        const auto& a = detail::api();
        if (!a.loaded || !ctx_) return cfire::make_error("invalid TLS ServerContext");
        if (conn.fd() < 0) return cfire::make_error("cannot wrap invalid TCP socket in TLS");

        void* ssl = a.SSL_new(ctx_.get());
        if (!ssl) return cfire::make_error("SSL_new failed");

        (void)a.SSL_set_fd(ssl, conn.fd());
        if (a.SSL_accept(ssl) != 1) {
            std::string err = detail::last_openssl_error();
            a.SSL_free(ssl);
            return cfire::make_error(std::format("TLS server handshake failed: {}", err));
        }

        auto stream = std::make_shared<cfire::net::TlsStream>();
        stream->ssl = ssl;
        stream->owned_ctx = new std::shared_ptr<void>(ctx_);
        stream->read_fn = &detail::tls_read_bridge;
        stream->write_fn = &detail::tls_write_bridge;
        stream->close_fn = &detail::tls_close_shared_ctx;
        conn.set_tls(std::move(stream));
        return {};
    }

private:
    std::shared_ptr<void> ctx_{nullptr};
};

[[nodiscard]] inline auto wrap_client(
    cfire::net::TcpConn& conn,
    std::string_view server_name = "",
    bool verify_peer = true,
    std::string_view ca_file = ""
) -> std::expected<void, cfire::Error> {
    const auto& a = detail::api();
    if (!a.loaded) {
        return cfire::make_error("OpenSSL library (libssl/libcrypto) not available on system");
    }
    if (conn.fd() < 0) {
        return cfire::make_error("cannot establish TLS handshake on closed socket");
    }

    void* ctx = a.SSL_CTX_new(a.TLS_client_method());
    if (!ctx) return cfire::make_error("SSL_CTX_new(TLS_client_method) failed");

    static constexpr int kSslVerifyNone = 0x00;
    static constexpr int kSslVerifyPeer = 0x01;
    static constexpr int kSslCtrlSetTlsextHostname = 55;
    static constexpr long kTlsextNametypeHostName = 0;

    if (verify_peer) {
        if (a.SSL_CTX_set_default_verify_paths) {
            (void)a.SSL_CTX_set_default_verify_paths(ctx);
        }
        if (!ca_file.empty() && a.SSL_CTX_load_verify_locations) {
            std::string ca_path(ca_file);
            if (a.SSL_CTX_load_verify_locations(ctx, ca_path.c_str(), nullptr) != 1) {
                a.SSL_CTX_free(ctx);
                return cfire::make_error(std::format("failed to load CA bundle '{}'", ca_file));
            }
        } else if (a.SSL_CTX_load_verify_locations) {
            if (std::filesystem::exists("/etc/ssl/cert.pem")) {
                (void)a.SSL_CTX_load_verify_locations(ctx, "/etc/ssl/cert.pem", nullptr);
            } else if (std::filesystem::exists("/opt/homebrew/etc/openssl@3/cert.pem")) {
                (void)a.SSL_CTX_load_verify_locations(ctx, "/opt/homebrew/etc/openssl@3/cert.pem", nullptr);
            }
        }
        if (a.SSL_CTX_set_verify) {
            a.SSL_CTX_set_verify(ctx, kSslVerifyPeer, nullptr);
        }
    } else {
        if (a.SSL_CTX_set_verify) {
            a.SSL_CTX_set_verify(ctx, kSslVerifyNone, nullptr);
        }
    }

    void* ssl = a.SSL_new(ctx);
    if (!ssl) {
        a.SSL_CTX_free(ctx);
        return cfire::make_error("SSL_new failed");
    }

    if (!server_name.empty()) {
        std::string host_str(server_name);
        if (a.SSL_ctrl) {
            (void)a.SSL_ctrl(ssl, kSslCtrlSetTlsextHostname, kTlsextNametypeHostName, const_cast<char*>(host_str.c_str()));
        }
        if (verify_peer && a.SSL_set1_host && host_str != "127.0.0.1") {
            (void)a.SSL_set1_host(ssl, host_str.c_str());
        }
    }

    (void)a.SSL_set_fd(ssl, conn.fd());
    if (a.SSL_connect(ssl) != 1) {
        long verify_res = a.SSL_get_verify_result ? a.SSL_get_verify_result(ssl) : 0;
        std::string err = detail::last_openssl_error();
        a.SSL_free(ssl);
        a.SSL_CTX_free(ctx);
        if (verify_peer && verify_res != 0) {
            return cfire::make_error(std::format("TLS certificate verification failed (code {}): {}", verify_res, err));
        }
        return cfire::make_error(std::format("TLS client handshake failed: {}", err));
    }

    auto stream = std::make_shared<cfire::net::TlsStream>();
    stream->ssl = ssl;
    stream->owned_ctx = ctx;
    stream->read_fn = &detail::tls_read_bridge;
    stream->write_fn = &detail::tls_write_bridge;
    stream->close_fn = &detail::tls_close_owned_ctx;
    conn.set_tls(std::move(stream));
    return {};
}

[[nodiscard]] inline auto connect(
    std::string_view host,
    std::uint16_t port = 443,
    bool verify_peer = true,
    std::string_view ca_file = ""
) -> std::expected<cfire::net::TcpConn, cfire::Error> {
    auto conn_res = cfire::net::connect_tcp(host, port);
    if (!conn_res) return std::unexpected(conn_res.error());
    auto wrap_res = wrap_client(*conn_res, host, verify_peer, ca_file);
    if (!wrap_res) return std::unexpected(wrap_res.error());
    return std::move(*conn_res);
}

} // namespace cfire::tls
