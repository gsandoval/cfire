#pragma once
// src/module_registry.hpp — Single Source of Truth for cfire Standard Library & Vendor Module Metadata

#include "docgen/catalog_types.hpp"
#include <algorithm>
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

[[nodiscard]] inline auto known_module_paths() -> const std::vector<std::string_view>& {
    static const auto kModules = []() {
        std::vector<std::string_view> v;
        for (const auto& mod : cfire::doc::stdlib_catalog()) {
            v.push_back(mod.import_path);
            for (const auto& a : mod.aliases) {
                v.push_back(a);
            }
        }
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
        return v;
    }();
    return kModules;
}

[[nodiscard]] inline auto is_known_module(std::string_view raw_path) -> bool {
    for (std::string_view m : known_module_paths()) {
        if (m == raw_path) return true;
    }
    return false;
}

[[nodiscard]] inline auto canonical_import_for_wrong_prefix(std::string_view raw_path) -> std::string {
    if (raw_path == "core:ai" || raw_path.starts_with("core:ai/")) return "ext:ai";
    if (raw_path == "core:compress/zstd" || raw_path == "core:compress" || raw_path == "core:zstd" || raw_path.starts_with("core:compress/")) return "ext:compress/zstd";
    if (raw_path == "core:crypto/sodium" || raw_path == "core:sodium") return "ext:crypto/sodium";
    if (raw_path == "core:db/sqlite" || raw_path == "core:sqlite") return "ext:db/sqlite";
    if (raw_path == "core:net/ssh" || raw_path == "core:ssh") return "ext:net/ssh";
    if (raw_path == "core:webui") return "ext:webui";

    if (raw_path.starts_with("ext:")) {
        std::string tail(raw_path.substr(4));
        if (tail == "ai" || tail.starts_with("ai/") ||
            tail == "compress/zstd" || tail == "compress" || tail.starts_with("compress/") || tail == "zstd" ||
            tail == "crypto/sodium" || tail == "sodium" ||
            tail == "db/sqlite" || tail == "sqlite" ||
            tail == "net/ssh" || tail == "ssh" ||
            tail == "webui" || tail == "tako") {
            return {};
        }
        if (tail == "url") return "core:net/url";
        if (tail == "http") return "core:net/http";
        if (tail == "tls") return "core:net/tls";
        if (tail == "websocket") return "core:net/websocket";
        if (tail == "os/keychain") return "core:keychain";
        if (tail == "context") return "core:sync/context";
        if (tail == "base64" || tail == "csv" || tail == "hex") return "core:encoding/" + tail;
        if (tail == "aes" || tail == "blake3" || tail == "hmac" || tail == "sha256") return "core:crypto/" + tail;
        std::string core_candidate = "core:" + tail;
        if (is_known_module(core_candidate)) {
            return core_candidate;
        }
        return {};
    }
    return {};
}

[[nodiscard]] inline auto normalize_import_path(std::string_view raw_path) -> std::string {
    if (raw_path.starts_with("ext:")) {
        return "core:" + std::string(raw_path.substr(4));
    }
    return std::string(raw_path);
}

[[nodiscard]] inline auto canonical_ns_for_import(std::string_view raw_path) -> std::pair<std::string, bool> {
    for (const auto& mod : cfire::doc::stdlib_catalog()) {
        if (mod.import_path == raw_path) {
            return {mod.ns_alias, mod.is_vendor};
        }
        for (const auto& a : mod.aliases) {
            if (a == raw_path) {
                return {mod.ns_alias, mod.is_vendor};
            }
        }
    }
    std::string path = normalize_import_path(raw_path);
    for (const auto& mod : cfire::doc::stdlib_catalog()) {
        if (mod.import_path == path) {
            return {mod.ns_alias, mod.is_vendor};
        }
    }
    return {"", false};
}

[[nodiscard]] inline auto resolve_import(std::string_view raw_path) -> ResolvedImport {
    ResolvedImport info;

    if (!canonical_import_for_wrong_prefix(raw_path).empty()) {
        info.is_core = false;
        return info;
    }

    std::string path = normalize_import_path(raw_path);

    for (const auto& mod : cfire::doc::stdlib_catalog()) {
        bool matches = (mod.import_path == raw_path || mod.import_path == path);
        if (!matches) {
            for (const auto& a : mod.aliases) {
                if (a == raw_path || a == path) {
                    matches = true;
                    break;
                }
            }
        }
        if (matches) {
            if (mod.is_vendor) {
                info.is_vendor = true;
                info.vendor_key = mod.ns_alias;
                info.implied_vendors = mod.implied_vendors;
                info.link_tags = mod.link_tags;
                return info;
            }
            info.is_core = true;
            std::string k = mod.core_key.empty() ? mod.ns_alias : mod.core_key;
            info.core_keys.push_back(k);
            for (const auto& ek : mod.extra_core_keys) {
                info.core_keys.push_back(ek);
            }
            info.defines = mod.defines;
            info.link_tags = mod.link_tags;
#if defined(__APPLE__)
            if (mod.ns_alias == "keychain") {
                info.link_tags = {"-framework Security", "-framework CoreFoundation"};
            }
#endif
            return info;
        }
    }
    return info;
}

[[nodiscard]] inline auto core_module_header(std::string_view mod) -> std::string {
    for (const auto& m : cfire::doc::stdlib_catalog()) {
        std::string k = m.core_key.empty() ? m.ns_alias : m.core_key;
        if (m.ns_alias == mod || k == mod || m.import_path == mod) {
            if (!m.header_path.empty()) return m.header_path;
        }
        for (const auto& a : m.aliases) {
            if (a == mod) {
                if (!m.header_path.empty()) return m.header_path;
            }
        }
    }
    return std::format("core/{}.hpp", mod);
}

[[nodiscard]] inline auto is_known_core_namespace(std::string_view name) noexcept -> bool {
    for (const auto& mod : cfire::doc::stdlib_catalog()) {
        if (mod.ns_alias == name) return true;
        for (const auto& a : mod.aliases) {
            auto colon = a.rfind(':');
            auto slash = a.rfind('/');
            std::size_t sep = std::string::npos;
            if (colon != std::string::npos && slash != std::string::npos) sep = std::max(colon, slash);
            else if (colon != std::string::npos) sep = colon;
            else sep = slash;
            std::string alias_ns = (sep == std::string::npos) ? a : a.substr(sep + 1);
            if (alias_ns == name) return true;
        }
    }
    return false;
}

} // namespace cfire::modules
