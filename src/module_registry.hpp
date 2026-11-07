#pragma once
// src/module_registry.hpp — Single Source of Truth for cfire Standard Library & Vendor Module Metadata

#include <array>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace cfire::modules {

struct ResolvedImport {
    bool is_core = false;
    bool is_vendor = false;
    std::vector<std::string> core_keys{};
    std::string vendor_key{};
    std::vector<std::string> implied_vendors{};
    std::vector<std::string> defines{};
    std::vector<std::string> link_tags{};
};

[[nodiscard]] inline auto normalize_import_path(std::string_view raw_path) -> std::string {
    if (raw_path.starts_with("ext:")) {
        return "core:" + std::string(raw_path.substr(4));
    }
    return std::string(raw_path);
}

[[nodiscard]] inline auto resolve_import(std::string_view raw_path) -> ResolvedImport {
    std::string path = normalize_import_path(raw_path);
    ResolvedImport info;

    if (path == "vendor:raylib") {
        info.is_vendor = true;
        info.vendor_key = "raylib";
        info.link_tags = {"-lraylib"};
        return info;
    }
    if (path == "vendor:raygui") {
        info.is_vendor = true;
        info.vendor_key = "raygui";
        info.implied_vendors = {"raylib"};
        info.link_tags = {"-lraylib"};
        return info;
    }

    if (!path.starts_with("core:")) {
        return info;
    }

    info.is_core = true;
    if (path == "core:fs") {
        info.core_keys = {"fs"};
    } else if (path == "core:cli") {
        info.core_keys = {"cli"};
    } else if (path.starts_with("core:compress/") || path == "core:compress" || path == "core:zstd") {
        info.core_keys = {"compress"};
        info.defines = {"#define CFIRE_USE_ZSTD 1"};
        info.link_tags = {"-lzstd"};
    } else if (path == "core:json") {
        info.core_keys = {"json"};
        info.defines = {"#define CFIRE_USE_SIMDJSON 1"};
        info.link_tags = {"-lsimdjson"};
    } else if (path == "core:db/sqlite" || path == "core:sqlite") {
        info.core_keys = {"sqlite"};
        info.defines = {"#define CFIRE_USE_SQLITE 1"};
        info.link_tags = {"-lsqlite3"};
    } else if (raw_path == "ext:crypto/sodium" || raw_path == "ext:sodium") {
        info.core_keys = {"sodium"};
        info.defines = {"#define CFIRE_USE_SODIUM 1"};
        info.link_tags = {"@pkg:libsodium"};
    } else if (raw_path == "core:crypto/sodium" || raw_path == "core:sodium") {
        info.is_core = false;
        return info;
    } else if (path == "core:keychain" || path == "core:os/keychain") {
        info.core_keys = {"keychain"};
#if defined(__APPLE__)
        info.link_tags = {"-framework Security", "-framework CoreFoundation"};
#endif
    } else if (path == "core:net/ssh" || path == "core:ssh") {
        info.core_keys = {"ssh"};
        info.link_tags = {"@pkg:libssh"};
    } else if (path.starts_with("core:crypto/") || path == "core:crypto") {
        info.core_keys = {"crypto"};
    } else if (path.starts_with("core:net/") || path == "core:net" || path == "core:http" ||
               path == "core:websocket" || path == "core:tls") {
        info.core_keys = {"net"};
    } else if (path == "core:time") {
        info.core_keys = {"time"};
    } else if (path == "core:os") {
        info.core_keys = {"os"};
    } else if (path.starts_with("core:sync/") || path == "core:sync" || path == "core:context") {
        info.core_keys = {"sync"};
    } else if (path == "core:str") {
        info.core_keys = {"str"};
    } else if (path == "core:math" || path == "core:rand") {
        info.core_keys = {"math"};
    } else if (path.starts_with("core:encoding/") || path == "core:encoding" ||
               path == "core:base64" || path == "core:hex" || path == "core:csv") {
        info.core_keys = {"encoding"};
    } else if (path == "core:regex") {
        info.core_keys = {"regex"};
    } else if (path == "core:collections" || path == "core:sort") {
        info.core_keys = {"collections"};
    } else if (path == "core:log") {
        info.core_keys = {"log"};
    } else if (path == "core:unicode") {
        info.core_keys = {"unicode"};
    } else if (path == "core:audio") {
        info.core_keys = {"audio"};
    } else if (path == "core:ai" || path.starts_with("core:ai/")) {
        info.core_keys = {"ai"};
        info.defines = {"#define CFIRE_USE_SIMDJSON 1"};
        info.link_tags = {"-lsimdjson"};
    } else if (path == "core:tako") {
        info.core_keys = {"tako", "json"};
        info.defines = {"#define CFIRE_USE_SIMDJSON 1"};
        info.link_tags = {"-lsimdjson"};
    } else if (path == "core:webui") {
        info.core_keys = {"webui", "json"};
        info.defines = {"#define CFIRE_USE_SIMDJSON 1"};
        info.link_tags = {"-lsimdjson"};
    } else {
        info.core_keys = {path.substr(5)};
    }
    return info;
}

[[nodiscard]] inline auto core_module_header(std::string_view mod) -> std::string {
    if (mod == "cli") return "core/cli.hpp";
    if (mod == "collections" || mod == "sort") return "core/collections.hpp";
    if (mod == "zstd" || mod == "compress") return "core/compress.hpp";
    if (mod == "crypto") return "core/crypto.hpp";
    if (mod == "encoding" || mod == "base64" || mod == "hex" || mod == "csv") return "core/encoding.hpp";
    if (mod == "fs") return "core/fs.hpp";
    if (mod == "json") return "core/json.hpp";
    if (mod == "log") return "core/log.hpp";
    if (mod == "math" || mod == "rand") return "core/math.hpp";
    if (mod == "net" || mod == "http" || mod == "websocket" || mod == "url" || mod == "tls") return "core/net.hpp";
    if (mod == "os") return "core/os.hpp";
    if (mod == "regex") return "core/regex.hpp";
    if (mod == "sqlite") return "core/sqlite.hpp";
    if (mod == "str") return "core/str.hpp";
    if (mod == "sync" || mod == "context") return "core/sync.hpp";
    if (mod == "time") return "core/time.hpp";
    if (mod == "unicode") return "core/unicode.hpp";
    if (mod == "audio") return "core/audio.hpp";
    return std::format("core/{}.hpp", mod);
}

[[nodiscard]] constexpr auto is_known_core_namespace(std::string_view name) noexcept -> bool {
    constexpr std::array<std::string_view, 35> kNamespaces = {
        "fs", "zstd", "json", "sqlite", "blake3", "net", "cli",
        "time", "os", "sync", "str", "math", "rand", "log", "regex",
        "collections", "sort", "unicode", "base64", "hex", "csv",
        "aes", "hmac", "sha256", "sodium", "keychain", "ssh", "webui",
        "context", "websocket", "http", "url", "tls", "audio", "ai"
    };
    for (std::string_view ns : kNamespaces) {
        if (ns == name) return true;
    }
    return false;
}

} // namespace cfire::modules
