#pragma once
#include "../../cfire_prelude.hpp"

#include <string>
#include <string_view>
#include <expected>
#include <unordered_map>
#include <cctype>
#include <algorithm>

// --- `core:net/url` (RFC 3986 URL Parser & Form URL Encoding) ---
namespace cfire::url {

struct Url {
    std::string scheme = "http";
    std::string host{};
    std::uint16_t port = 80;
    std::string path = "/";
    std::string query{};
    std::string fragment{};
    std::unordered_map<std::string, std::string> query_params{};
};

[[nodiscard]] inline auto encode(std::string_view sv) -> std::string {
    static constexpr char hex_chars[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(sv.size() * 3 / 2);
    for (char c : sv) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(c);
        } else {
            out.push_back('%');
            out.push_back(hex_chars[(static_cast<unsigned char>(c) >> 4) & 0x0f]);
            out.push_back(hex_chars[static_cast<unsigned char>(c) & 0x0f]);
        }
    }
    return out;
}

[[nodiscard]] inline auto decode(std::string_view sv) -> std::string {
    std::string out;
    for (std::size_t i = 0; i < sv.size(); ++i) {
        if (sv[i] == '%' && i + 2 < sv.size()) {
            int h1 = std::isdigit(sv[i + 1]) ? sv[i + 1] - '0' : std::tolower(sv[i + 1]) - 'a' + 10;
            int h2 = std::isdigit(sv[i + 2]) ? sv[i + 2] - '0' : std::tolower(sv[i + 2]) - 'a' + 10;
            out.push_back(static_cast<char>((h1 << 4) | h2));
            i += 2;
        } else if (sv[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(sv[i]);
        }
    }
    return out;
}

[[nodiscard]] inline auto parse(std::string_view raw) -> std::expected<Url, cfire::Error> {
    if (raw.empty()) return cfire::make_error("empty URL string");
    Url u;
    std::size_t scheme_end = raw.find("://");
    std::size_t host_start = 0;
    if (scheme_end != std::string_view::npos) {
        u.scheme = std::string(raw.substr(0, scheme_end));
        host_start = scheme_end + 3;
    } else {
        u.scheme = "http";
        host_start = 0;
    }

    std::size_t path_start = raw.find('/', host_start);
    std::size_t query_start = raw.find('?', host_start);
    std::size_t frag_start = raw.find('#', host_start);

    std::size_t host_end = raw.size();
    if (path_start != std::string_view::npos) host_end = std::min(host_end, path_start);
    if (query_start != std::string_view::npos) host_end = std::min(host_end, query_start);
    if (frag_start != std::string_view::npos) host_end = std::min(host_end, frag_start);

    std::string_view host_port = raw.substr(host_start, host_end - host_start);
    std::size_t colon = host_port.find(':');
    if (colon != std::string_view::npos) {
        u.host = std::string(host_port.substr(0, colon));
        try {
            u.port = static_cast<std::uint16_t>(std::stoi(std::string(host_port.substr(colon + 1))));
        } catch (...) {
            u.port = (u.scheme == "https") ? 443 : 80;
        }
    } else {
        u.host = std::string(host_port);
        u.port = (u.scheme == "https") ? 443 : 80;
    }

    if (frag_start != std::string_view::npos) {
        u.fragment = decode(raw.substr(frag_start + 1));
    }

    std::size_t query_end = (frag_start != std::string_view::npos) ? frag_start : raw.size();
    if (query_start != std::string_view::npos && query_start < query_end) {
        u.query = std::string(raw.substr(query_start + 1, query_end - (query_start + 1)));
        std::string_view qsv = u.query;
        std::size_t pos = 0;
        while (pos < qsv.size()) {
            std::size_t next_amp = qsv.find('&', pos);
            if (next_amp == std::string_view::npos) next_amp = qsv.size();
            std::string_view pair = qsv.substr(pos, next_amp - pos);
            std::size_t eq = pair.find('=');
            if (eq != std::string_view::npos) {
                u.query_params[decode(pair.substr(0, eq))] = decode(pair.substr(eq + 1));
            } else if (!pair.empty()) {
                u.query_params[decode(pair)] = "";
            }
            pos = next_amp + 1;
        }
    }

    std::size_t path_end = raw.size();
    if (query_start != std::string_view::npos) path_end = std::min(path_end, query_start);
    if (frag_start != std::string_view::npos) path_end = std::min(path_end, frag_start);

    if (path_start != std::string_view::npos && path_start < path_end) {
        u.path = std::string(raw.substr(path_start, path_end - path_start));
    } else {
        u.path = "/";
    }

    return u;
}

} // namespace cfire::url
