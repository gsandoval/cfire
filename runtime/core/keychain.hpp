#pragma once
#include "../cfire_prelude.hpp"
#include "os.hpp"

#include <atomic>
#include <cstdio>
#include <expected>
#include <format>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#if defined(__APPLE__) && !defined(PLATFORM_WEB)
#include <TargetConditionals.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <dlfcn.h>
#include <objc/runtime.h>
#include <objc/message.h>
#ifndef CFIRE_OBJC_AUTORELEASE_DECLARED
#define CFIRE_OBJC_AUTORELEASE_DECLARED 1
extern "C" {
void* objc_autoreleasePoolPush(void);
void objc_autoreleasePoolPop(void*);
}
#endif
#ifndef CFIRE_OBJC_STACK_BLOCK_DECLARED
#define CFIRE_OBJC_STACK_BLOCK_DECLARED 1
extern "C" {
extern void* _NSConcreteStackBlock[];
}
#endif
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
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

    // update in place, and only add when the item does not exist yet. The previous
    // delete-then-add lost the stored secret whenever SecItemAdd failed (e.g. a locked keychain).
    const void* upd_keys[] = { kSecValueData };
    const void* upd_vals[] = { cf_secret };
    CFDictionaryRef upd_dict = CFDictionaryCreate(kCFAllocatorDefault, upd_keys, upd_vals, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    auto u_guard = cfire::scope_exit([&] { if (upd_dict) CFRelease(upd_dict); });
    OSStatus upd_status = SecItemUpdate(query, upd_dict);
    if (upd_status == errSecSuccess) return {};
    if (upd_status != errSecItemNotFound) {
        return cfire::make_error(std::format("macOS Keychain SecItemUpdate failed (OSStatus {})", static_cast<int>(upd_status)));
    }

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
    std::span<const std::uint8_t> secret
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

namespace detail {

#if defined(CFIRE_TEST_MODE)
inline auto mock_biometric_slot() -> std::pair<std::mutex&, std::string&> {
    static std::mutex mu;
    static std::string mode;
    return {mu, mode};
}

inline void set_mock_biometric(std::string_view mode) {
    auto [mu, slot] = mock_biometric_slot();
    std::lock_guard<std::mutex> lk(mu);
    slot.assign(mode);
}

inline void clear_mock_biometric() {
    auto [mu, slot] = mock_biometric_slot();
    std::lock_guard<std::mutex> lk(mu);
    slot.clear();
}
#endif

#if defined(__APPLE__) && !defined(PLATFORM_WEB)
[[nodiscard]] inline auto kc_objc_cls(const char* name) noexcept -> void* {
#if defined(__OBJC__) && __has_feature(objc_arc)
    return (__bridge void*)objc_getClass(name);
#else
    return reinterpret_cast<void*>(objc_getClass(name));
#endif
}

[[nodiscard]] inline auto ns_error_info(void* err_obj, std::string_view fallback) -> std::pair<int, std::string> {
    if (!err_obj) return {0, std::string(fallback)};
    using FnCode = long (*)(void*, SEL);
    using FnObj = void* (*)(void*, SEL);
    using FnUtf8 = const char* (*)(void*, SEL);
    long code = reinterpret_cast<FnCode>(objc_msgSend)(err_obj, sel_registerName("code"));
    void* desc_ns = reinterpret_cast<FnObj>(objc_msgSend)(err_obj, sel_registerName("localizedDescription"));
    const char* desc_c = desc_ns ? reinterpret_cast<FnUtf8>(objc_msgSend)(desc_ns, sel_registerName("UTF8String")) : nullptr;
    std::string msg = (desc_c && *desc_c) ? std::string(desc_c) : std::string(fallback);
    return {static_cast<int>(code), std::move(msg)};
}

[[nodiscard]] inline auto is_gui_main_thread() noexcept -> bool {
    void* nsthread_cls = kc_objc_cls("NSThread");
    if (!nsthread_cls) return false;
    using FnBool = BOOL (*)(void*, SEL);
    if (reinterpret_cast<FnBool>(objc_msgSend)(nsthread_cls, sel_registerName("isMainThread")) == 0) {
        return false;
    }
#if !TARGET_OS_IPHONE
    void* ns_app_sym = ::dlsym(RTLD_DEFAULT, "NSApp");
    if (!ns_app_sym) return false;
    void* app = *reinterpret_cast<void**>(ns_app_sym);
    if (!app) return false;
    return reinterpret_cast<FnBool>(objc_msgSend)(app, sel_registerName("isRunning")) != 0;
#else
    void* uiapp_cls = kc_objc_cls("UIApplication");
    if (!uiapp_cls) return false;
    using FnApp = void* (*)(void*, SEL);
    void* app = reinterpret_cast<FnApp>(objc_msgSend)(uiapp_cls, sel_registerName("sharedApplication"));
    return app != nullptr;
#endif
}

struct LaReplyState {
    std::promise<std::expected<bool, cfire::Error>> promise{};
    std::atomic<bool> completed{false};
};

struct LaBlockDescriptor {
    unsigned long reserved;
    unsigned long size;
    void (*copy_helper)(void* dst, void* src);
    void (*dispose_helper)(void* src);
};

struct LaReplyBlock {
    void* isa;
    int flags;
    int reserved;
    void (*invoke)(void*, BOOL, void*);
    const LaBlockDescriptor* descriptor;
    std::shared_ptr<LaReplyState>* state_holder;
};

inline void la_block_copy(void* dst_raw, void* src_raw) {
    auto* dst = static_cast<LaReplyBlock*>(dst_raw);
    auto* src = static_cast<LaReplyBlock*>(src_raw);
    dst->state_holder = new std::shared_ptr<LaReplyState>(*src->state_holder);
}

inline void la_block_dispose(void* src_raw) {
    auto* src = static_cast<LaReplyBlock*>(src_raw);
    delete src->state_holder;
    src->state_holder = nullptr;
}

inline void la_block_invoke(void* blk_raw, BOOL success, void* err_obj) {
    void* pool = ::objc_autoreleasePoolPush();
    auto pool_guard = cfire::scope_exit([&] { ::objc_autoreleasePoolPop(pool); });

    auto* blk = static_cast<LaReplyBlock*>(blk_raw);
    if (!blk || !blk->state_holder || !*blk->state_holder) return;
    auto state = *blk->state_holder;
    if (state->completed.exchange(true, std::memory_order_acq_rel)) return;

    if (success != 0) {
        state->promise.set_value(true);
        return;
    }
    if (err_obj) {
        auto [code, desc] = ns_error_info(err_obj, "biometric evaluation failed");
        // kLAErrorAuthenticationFailed (-1) and kLAErrorUserCancel (-2)
        if (code == -1 || code == -2) {
            state->promise.set_value(false);
        } else {
            state->promise.set_value(cfire::make_error(desc, code));
        }
        return;
    }
    state->promise.set_value(false);
}
#endif

} // namespace detail

[[nodiscard]] inline auto authenticate_biometric(
    std::string_view reason,
    bool allow_passcode = true
) -> std::expected<bool, cfire::Error> {
    if (reason.empty()) {
        return cfire::make_error("keychain.authenticate_biometric requires a non-empty reason");
    }
#if defined(CFIRE_TEST_MODE)
    if (detail::is_headless_test()) {
        std::string mode;
        {
            auto [mu, slot] = detail::mock_biometric_slot();
            std::lock_guard<std::mutex> lk(mu);
            mode = slot;
        }
        if (mode.empty()) {
            if (const char* env_m = std::getenv("CFIRE_MOCK_BIOMETRIC"); env_m && *env_m) {
                mode.assign(env_m);
            }
        }
        if (mode.empty() || mode == "1" || mode == "true") {
            return true;
        }
        if (mode == "0" || mode == "false") {
            return false;
        }
        if (mode == "err" || mode == "error") {
            return cfire::make_error("biometric authentication unavailable (mock)");
        }
        return cfire::make_error(std::format("biometric authentication mock error: {}", mode));
    }
#endif
#if defined(__APPLE__) && !defined(PLATFORM_WEB)
    if (detail::is_gui_main_thread()) {
        return cfire::make_error("biometric authentication must run on a background thread (e.g. bind_async)");
    }

    void* pool = ::objc_autoreleasePoolPush();
    auto pool_guard = cfire::scope_exit([&] { ::objc_autoreleasePoolPop(pool); });

    static void* la_lib = ::dlopen(
        "/System/Library/Frameworks/LocalAuthentication.framework/LocalAuthentication",
        RTLD_LAZY);
    (void)la_lib;

    void* la_cls = detail::kc_objc_cls("LAContext");
    void* nsstr_cls = detail::kc_objc_cls("NSString");
    if (!la_cls || !nsstr_cls) {
        return cfire::make_error("LocalAuthentication framework unavailable on this system");
    }

    using FnZero = void* (*)(void*, SEL);
    void* ctx_alloc = reinterpret_cast<FnZero>(objc_msgSend)(la_cls, sel_registerName("alloc"));
    void* ctx = ctx_alloc ? reinterpret_cast<FnZero>(objc_msgSend)(ctx_alloc, sel_registerName("init")) : nullptr;
    if (!ctx) {
        return cfire::make_error("keychain.authenticate_biometric failed to allocate LAContext");
    }
    auto ctx_guard = cfire::scope_exit([&] {
        reinterpret_cast<void (*)(void*, SEL)>(objc_msgSend)(ctx, sel_registerName("release"));
    });

    // LAPolicyDeviceOwnerAuthentication (2) or LAPolicyDeviceOwnerAuthenticationWithBiometrics (1)
    const long policy = allow_passcode ? 2L : 1L;
    void* can_err = nullptr;
    using FnCanEval = BOOL (*)(void*, SEL, long, void**);
    BOOL can_eval = reinterpret_cast<FnCanEval>(objc_msgSend)(
        ctx, sel_registerName("canEvaluatePolicy:error:"), policy, &can_err);
    if (can_eval == 0) {
        auto [code, desc] = detail::ns_error_info(can_err, "biometric authentication is not available");
        return cfire::make_error(desc, code);
    }

    std::string reason_str(reason);
    using FnStrUtf8 = void* (*)(void*, SEL, const char*);
    void* ns_reason = reinterpret_cast<FnStrUtf8>(objc_msgSend)(
        nsstr_cls, sel_registerName("stringWithUTF8String:"), reason_str.c_str());
    if (!ns_reason) {
        return cfire::make_error("keychain.authenticate_biometric failed to encode reason string");
    }

    auto state = std::make_shared<detail::LaReplyState>();
    auto fut = state->promise.get_future();

    static const detail::LaBlockDescriptor kDesc{
        0,
        sizeof(detail::LaReplyBlock),
        detail::la_block_copy,
        detail::la_block_dispose,
    };
    detail::LaReplyBlock reply_blk{
        _NSConcreteStackBlock,
        (1 << 25),
        0,
        detail::la_block_invoke,
        &kDesc,
        &state,
    };

    using FnEval = void (*)(void*, SEL, long, void*, void*);
    reinterpret_cast<FnEval>(objc_msgSend)(
        ctx, sel_registerName("evaluatePolicy:localizedReason:reply:"), policy, ns_reason, &reply_blk);
    return fut.get();
#else
    (void)allow_passcode;
    return cfire::make_error("biometric authentication is not supported on this platform");
#endif
}

} // namespace cfire::keychain
