#pragma once
#include "../cfire_prelude.hpp"
#include "os.hpp"

#include <cstdio>
#include <expected>
#include <format>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#if defined(__APPLE__) && !defined(PLATFORM_WEB)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#elif defined(_WIN32)
#include <windows.h>
#include <wincred.h>
#endif

namespace cfire::keychain {

namespace detail {

#if defined(CFIRE_TEST_MODE)
inline auto memory_fallback_store() -> std::pair<std::mutex&, std::unordered_map<std::string, std::string>&> {
    static std::mutex mu;
    static std::unordered_map<std::string, std::string> store;
    return {mu, store};
}

inline auto is_headless_test() noexcept -> bool {
    if (const char* env_h = std::getenv("CFIRE_HEADLESS_TEST"); env_h && std::string_view(env_h) == "1") {
        return true;
    }
    return false;
}
#endif

inline auto posix_shell_quote(std::string_view s) -> std::string {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

#if defined(_WIN32)
inline auto utf8_to_wide(std::string_view s) -> std::wstring {
    if (s.empty()) return {};
    int needed = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), needed);
    return out;
}
#endif

} // namespace detail

[[nodiscard]] inline auto set(
    std::string_view service,
    std::string_view account,
    std::string_view secret
) -> std::expected<void, cfire::Error> {
    if (service.empty() || account.empty()) {
        return cfire::make_error("keychain.set requires non-empty service and account");
    }
#if defined(CFIRE_TEST_MODE)
    if (detail::is_headless_test()) {
        std::string key_id = std::format("{}:{}", service, account);
        auto [mu, store] = detail::memory_fallback_store();
        std::lock_guard<std::mutex> lk(mu);
        store[key_id] = std::string(secret);
        return {};
    }
#endif

#if defined(__APPLE__) && !defined(PLATFORM_WEB)
    CFStringRef cf_service = CFStringCreateWithBytes(kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(service.data()), static_cast<CFIndex>(service.size()),
        kCFStringEncodingUTF8, false);
    CFStringRef cf_account = CFStringCreateWithBytes(kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(account.data()), static_cast<CFIndex>(account.size()),
        kCFStringEncodingUTF8, false);
    CFDataRef cf_secret = CFDataCreate(kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(secret.data()), static_cast<CFIndex>(secret.size()));

    auto guard = cfire::scope_exit([&] {
        if (cf_service) CFRelease(cf_service);
        if (cf_account) CFRelease(cf_account);
        if (cf_secret) CFRelease(cf_secret);
    });
    if (!cf_service || !cf_account || !cf_secret) {
        return cfire::make_error("keychain.set failed to allocate CoreFoundation objects");
    }

    const void* query_keys[] = { kSecClass, kSecAttrService, kSecAttrAccount };
    const void* query_vals[] = { kSecClassGenericPassword, cf_service, cf_account };
    CFDictionaryRef query = CFDictionaryCreate(kCFAllocatorDefault, query_keys, query_vals, 3,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    auto q_guard = cfire::scope_exit([&] { if (query) CFRelease(query); });

    SecItemDelete(query);

    const void* add_keys[] = { kSecClass, kSecAttrService, kSecAttrAccount, kSecValueData };
    const void* add_vals[] = { kSecClassGenericPassword, cf_service, cf_account, cf_secret };
    CFDictionaryRef add_dict = CFDictionaryCreate(kCFAllocatorDefault, add_keys, add_vals, 4,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    auto a_guard = cfire::scope_exit([&] { if (add_dict) CFRelease(add_dict); });

    OSStatus status = SecItemAdd(add_dict, nullptr);
    if (status != errSecSuccess) {
        return cfire::make_error(std::format("macOS Keychain SecItemAdd failed (OSStatus {})", static_cast<int>(status)));
    }
    return {};
#elif defined(__linux__) && !defined(PLATFORM_WEB)
    // Stream secret via stdin pipe so it never appears in shell command strings or /proc/<pid>/cmdline.
    std::string q_svc = detail::posix_shell_quote(service);
    std::string q_acc = detail::posix_shell_quote(account);
    std::string cmd = std::format(
        "secret-tool store --label={} service {} account {} >/dev/null 2>&1",
        q_svc, q_svc, q_acc);
    FILE* pipe = ::popen(cmd.c_str(), "w");
    if (!pipe) {
        return cfire::make_error("keychain.set failed to spawn secret-tool");
    }
    bool write_ok = true;
    if (!secret.empty()) {
        write_ok = (std::fwrite(secret.data(), 1, secret.size(), pipe) == secret.size());
    }
    int rc = ::pclose(pipe);
    if (!write_ok || rc != 0) {
        return cfire::make_error("keychain.set failed: secret-tool store returned an error (ensure libsecret-tools and D-Bus session are active)");
    }
    return {};
#elif defined(_WIN32)
    if (secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
        return cfire::make_error(std::format("keychain.set secret exceeds Windows Credential Manager limit ({} bytes)", CRED_MAX_CREDENTIAL_BLOB_SIZE));
    }
    std::wstring target_name = detail::utf8_to_wide(std::format("{}/{}", service, account));
    std::wstring user_name = detail::utf8_to_wide(account);
    std::vector<BYTE> blob(secret.begin(), secret.end());
    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = target_name.data();
    cred.CredentialBlobSize = static_cast<DWORD>(blob.size());
    cred.CredentialBlob = blob.empty() ? nullptr : blob.data();
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.UserName = user_name.data();
    if (!::CredWriteW(&cred, 0)) {
        return cfire::make_error(std::format("Windows CredWriteW failed (error {})", static_cast<unsigned long>(::GetLastError())));
    }
    return {};
#else
    return cfire::make_error("keychain.set is not supported on this platform");
#endif
}

[[nodiscard]] inline auto set(
    std::string_view service,
    std::string_view account,
    std::span<const std::byte> secret
) -> std::expected<void, cfire::Error> {
    return set(service, account, std::string_view(reinterpret_cast<const char*>(secret.data()), secret.size()));
}

[[nodiscard]] inline auto get(
    std::string_view service,
    std::string_view account
) -> std::expected<std::string, cfire::Error> {
    if (service.empty() || account.empty()) {
        return cfire::make_error("keychain.get requires non-empty service and account");
    }
#if defined(CFIRE_TEST_MODE)
    if (detail::is_headless_test()) {
        std::string key_id = std::format("{}:{}", service, account);
        auto [mu, store] = detail::memory_fallback_store();
        std::lock_guard<std::mutex> lk(mu);
        if (auto it = store.find(key_id); it != store.end()) {
            return it->second;
        }
        return cfire::make_error(std::format("keychain item not found for service='{}', account='{}'", service, account));
    }
#endif

#if defined(__APPLE__) && !defined(PLATFORM_WEB)
    CFStringRef cf_service = CFStringCreateWithBytes(kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(service.data()), static_cast<CFIndex>(service.size()),
        kCFStringEncodingUTF8, false);
    CFStringRef cf_account = CFStringCreateWithBytes(kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(account.data()), static_cast<CFIndex>(account.size()),
        kCFStringEncodingUTF8, false);
    auto guard = cfire::scope_exit([&] {
        if (cf_service) CFRelease(cf_service);
        if (cf_account) CFRelease(cf_account);
    });

    const void* query_keys[] = { kSecClass, kSecAttrService, kSecAttrAccount, kSecReturnData, kSecMatchLimit };
    const void* query_vals[] = { kSecClassGenericPassword, cf_service, cf_account, kCFBooleanTrue, kSecMatchLimitOne };
    CFDictionaryRef query = CFDictionaryCreate(kCFAllocatorDefault, query_keys, query_vals, 5,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    auto q_guard = cfire::scope_exit([&] { if (query) CFRelease(query); });

    CFTypeRef result = nullptr;
    OSStatus status = SecItemCopyMatching(query, &result);
    if (status == errSecSuccess && result != nullptr) {
        CFDataRef data = static_cast<CFDataRef>(result);
        std::string secret(
            reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
            static_cast<std::size_t>(CFDataGetLength(data))
        );
        CFRelease(result);
        return secret;
    }
    return cfire::make_error(std::format("keychain item not found for service='{}', account='{}' (OSStatus {})",
        service, account, static_cast<int>(status)));
#elif defined(__linux__) && !defined(PLATFORM_WEB)
    std::string q_svc = detail::posix_shell_quote(service);
    std::string q_acc = detail::posix_shell_quote(account);
    std::string cmd = std::format("secret-tool lookup service {} account {} 2>/dev/null", q_svc, q_acc);
    FILE* pipe = ::popen(cmd.c_str(), "r");
    if (!pipe) {
        return cfire::make_error("keychain.get failed to spawn secret-tool");
    }
    std::string out;
    char buf[512];
    while (std::size_t n = std::fread(buf, 1, sizeof(buf), pipe)) {
        out.append(buf, n);
    }
    int rc = ::pclose(pipe);
    if (rc != 0) {
        return cfire::make_error(std::format("keychain item not found for service='{}', account='{}'", service, account));
    }
    if (!out.empty() && out.back() == '\n') {
        out.pop_back();
    }
    return out;
#elif defined(_WIN32)
    std::wstring target_name = detail::utf8_to_wide(std::format("{}/{}", service, account));
    PCREDENTIALW pcred = nullptr;
    if (::CredReadW(target_name.c_str(), CRED_TYPE_GENERIC, 0, &pcred) && pcred != nullptr) {
        std::string secret(reinterpret_cast<const char*>(pcred->CredentialBlob),
                           static_cast<std::size_t>(pcred->CredentialBlobSize));
        ::CredFree(pcred);
        return secret;
    }
    return cfire::make_error(std::format("keychain item not found for service='{}', account='{}'", service, account));
#else
    return cfire::make_error("keychain.get is not supported on this platform");
#endif
}

[[nodiscard]] inline auto delete_password(
    std::string_view service,
    std::string_view account
) -> std::expected<void, cfire::Error> {
    if (service.empty() || account.empty()) {
        return cfire::make_error("keychain.delete_password requires non-empty service and account");
    }
#if defined(CFIRE_TEST_MODE)
    if (detail::is_headless_test()) {
        std::string key_id = std::format("{}:{}", service, account);
        auto [mu, store] = detail::memory_fallback_store();
        std::lock_guard<std::mutex> lk(mu);
        store.erase(key_id);
        return {};
    }
#endif

#if defined(__APPLE__) && !defined(PLATFORM_WEB)
    CFStringRef cf_service = CFStringCreateWithBytes(kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(service.data()), static_cast<CFIndex>(service.size()),
        kCFStringEncodingUTF8, false);
    CFStringRef cf_account = CFStringCreateWithBytes(kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(account.data()), static_cast<CFIndex>(account.size()),
        kCFStringEncodingUTF8, false);
    auto guard = cfire::scope_exit([&] {
        if (cf_service) CFRelease(cf_service);
        if (cf_account) CFRelease(cf_account);
    });

    const void* query_keys[] = { kSecClass, kSecAttrService, kSecAttrAccount };
    const void* query_vals[] = { kSecClassGenericPassword, cf_service, cf_account };
    CFDictionaryRef query = CFDictionaryCreate(kCFAllocatorDefault, query_keys, query_vals, 3,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    auto q_guard = cfire::scope_exit([&] { if (query) CFRelease(query); });

    OSStatus status = SecItemDelete(query);
    if (status != errSecSuccess && status != errSecItemNotFound) {
        return cfire::make_error(std::format("macOS Keychain SecItemDelete failed (OSStatus {})", static_cast<int>(status)));
    }
    return {};
#elif defined(__linux__) && !defined(PLATFORM_WEB)
    std::string cmd = std::format(
        "secret-tool clear service {} account {} >/dev/null 2>&1",
        detail::posix_shell_quote(service), detail::posix_shell_quote(account));
    FILE* pipe = ::popen(cmd.c_str(), "r");
    if (!pipe) {
        return cfire::make_error("keychain.delete_password failed to spawn secret-tool");
    }
    int rc = ::pclose(pipe);
    if (rc != 0) {
        return cfire::make_error("keychain.delete_password failed: secret-tool clear returned an error");
    }
    return {};
#elif defined(_WIN32)
    std::wstring target_name = detail::utf8_to_wide(std::format("{}/{}", service, account));
    if (!::CredDeleteW(target_name.c_str(), CRED_TYPE_GENERIC, 0)) {
        DWORD err = ::GetLastError();
        if (err != ERROR_NOT_FOUND) {
            return cfire::make_error(std::format("Windows CredDeleteW failed (error {})", static_cast<unsigned long>(err)));
        }
    }
    return {};
#else
    return cfire::make_error("keychain.delete_password is not supported on this platform");
#endif
}

[[nodiscard]] inline auto remove(
    std::string_view service,
    std::string_view account
) -> std::expected<void, cfire::Error> {
    return delete_password(service, account);
}

} // namespace cfire::keychain
