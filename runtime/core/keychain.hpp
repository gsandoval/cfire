#pragma once
#include "../cfire_prelude.hpp"
#include "os.hpp"

#include <expected>
#include <format>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#if defined(__APPLE__) && !defined(PLATFORM_WEB)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#elif defined(_WIN32)
#include <windows.h>
#include <wincred.h>
#endif

namespace cfire::keychain {

namespace detail {

inline auto memory_fallback_store() -> std::pair<std::mutex&, std::unordered_map<std::string, std::string>&> {
    static std::mutex mu;
    static std::unordered_map<std::string, std::string> store;
    return {mu, store};
}

} // namespace detail

[[nodiscard]] inline auto set(
    std::string_view service,
    std::string_view account,
    std::string_view secret
) -> std::expected<void, cfire::Error> {
    if (service.empty() || account.empty()) {
        return cfire::make_error("keychain.set requires non-empty service and account");
    }
    std::string key_id = std::format("{}:{}", service, account);
    {
        auto [mu, store] = detail::memory_fallback_store();
        std::lock_guard<std::mutex> lk(mu);
        store[key_id] = std::string(secret);
    }
    if (const char* env_h = std::getenv("CFIRE_HEADLESS_TEST"); env_h && std::string_view(env_h) == "1") {
        return {};
    }

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
    (void)status;
#elif defined(__linux__) && !defined(PLATFORM_WEB)
    // Optional best-effort sync with libsecret (`secret-tool`) when running in a desktop session
    if (!cfire::os::getenv("DBUS_SESSION_BUS_ADDRESS").empty()) {
        std::string cmd = std::format(
            "printf '%s' '{}' | secret-tool store --label='{}' service '{}' account '{}' >/dev/null 2>&1",
            secret, service, service, account);
        (void)std::system(cmd.c_str());
    }
#elif defined(_WIN32)
    std::string target_name = std::format("{}/{}", service, account);
    std::string user_name(account);
    std::string secret_str(secret);
    CREDENTIALA cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = target_name.data();
    cred.CredentialBlobSize = static_cast<DWORD>(secret_str.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(secret_str.data());
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.UserName = user_name.data();
    (void)::CredWriteA(&cred, 0);
#endif
    return {};
}

[[nodiscard]] inline auto get(
    std::string_view service,
    std::string_view account
) -> std::expected<std::string, cfire::Error> {
    if (service.empty() || account.empty()) {
        return cfire::make_error("keychain.get requires non-empty service and account");
    }
    std::string key_id = std::format("{}:{}", service, account);
    if (const char* env_h = std::getenv("CFIRE_HEADLESS_TEST"); env_h && std::string_view(env_h) == "1") {
        auto [mu, store] = detail::memory_fallback_store();
        std::lock_guard<std::mutex> lk(mu);
        if (auto it = store.find(key_id); it != store.end()) {
            return it->second;
        }
        return cfire::make_error(std::format("keychain item not found for service='{}', account='{}'", service, account));
    }

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
#elif defined(_WIN32)
    std::string target_name = std::format("{}/{}", service, account);
    PCREDENTIALA pcred = nullptr;
    if (::CredReadA(target_name.c_str(), CRED_TYPE_GENERIC, 0, &pcred) && pcred != nullptr) {
        std::string secret(reinterpret_cast<const char*>(pcred->CredentialBlob),
                           static_cast<std::size_t>(pcred->CredentialBlobSize));
        ::CredFree(pcred);
        return secret;
    }
#endif

    {
        auto [mu, store] = detail::memory_fallback_store();
        std::lock_guard<std::mutex> lk(mu);
        if (auto it = store.find(key_id); it != store.end()) {
            return it->second;
        }
    }
    return cfire::make_error(std::format("keychain item not found for service='{}', account='{}'", service, account));
}

[[nodiscard]] inline auto delete_password(
    std::string_view service,
    std::string_view account
) -> std::expected<void, cfire::Error> {
    if (service.empty() || account.empty()) {
        return cfire::make_error("keychain.delete_password requires non-empty service and account");
    }
    std::string key_id = std::format("{}:{}", service, account);
    {
        auto [mu, store] = detail::memory_fallback_store();
        std::lock_guard<std::mutex> lk(mu);
        store.erase(key_id);
    }
    if (const char* env_h = std::getenv("CFIRE_HEADLESS_TEST"); env_h && std::string_view(env_h) == "1") {
        return {};
    }

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
    (void)status;
#elif defined(__linux__) && !defined(PLATFORM_WEB)
    if (!cfire::os::getenv("DBUS_SESSION_BUS_ADDRESS").empty()) {
        std::string cmd = std::format(
            "secret-tool clear service '{}' account '{}' >/dev/null 2>&1",
            service, account);
        (void)std::system(cmd.c_str());
    }
#elif defined(_WIN32)
    std::string target_name = std::format("{}/{}", service, account);
    (void)::CredDeleteA(target_name.c_str(), CRED_TYPE_GENERIC, 0);
#endif
    return {};
}

[[nodiscard]] inline auto remove(
    std::string_view service,
    std::string_view account
) -> std::expected<void, cfire::Error> {
    return delete_password(service, account);
}

} // namespace cfire::keychain
