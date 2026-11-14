#pragma once
// src/mcp_jsonrpc.hpp — Self-Contained JSON-RPC 2.0 MCP Server (Zero runtime/core/* dependencies)

#include "../runtime/cfire_prelude.hpp"
#include <cctype>
#include <charconv>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cfire::mcp {

struct JsonVal;
using JsonArray = std::vector<JsonVal>;
using JsonObject = std::map<std::string, JsonVal, std::less<>>;

struct JsonVal {
    using Variant = std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, JsonArray, JsonObject>;
    Variant data{nullptr};

    JsonVal() = default;
    JsonVal(std::nullptr_t) : data(nullptr) {}
    explicit JsonVal(bool b) : data(b) {}
    explicit JsonVal(std::int64_t v) : data(v) {}
    explicit JsonVal(double v) : data(v) {}
    explicit JsonVal(std::string s) : data(std::move(s)) {}
    explicit JsonVal(JsonArray a) : data(std::move(a)) {}
    explicit JsonVal(JsonObject o) : data(std::move(o)) {}

    [[nodiscard]] bool is_null() const noexcept { return std::holds_alternative<std::nullptr_t>(data); }
    [[nodiscard]] bool is_bool() const noexcept { return std::holds_alternative<bool>(data); }
    [[nodiscard]] bool is_int() const noexcept { return std::holds_alternative<std::int64_t>(data); }
    [[nodiscard]] bool is_double() const noexcept { return std::holds_alternative<double>(data); }
    [[nodiscard]] bool is_string() const noexcept { return std::holds_alternative<std::string>(data); }
    [[nodiscard]] bool is_array() const noexcept { return std::holds_alternative<JsonArray>(data); }
    [[nodiscard]] bool is_object() const noexcept { return std::holds_alternative<JsonObject>(data); }

    [[nodiscard]] auto as_str() const noexcept -> std::string_view {
        if (const auto* s = std::get_if<std::string>(&data)) return *s;
        return {};
    }

    [[nodiscard]] auto get(std::string_view key) const noexcept -> const JsonVal* {
        const auto* obj = std::get_if<JsonObject>(&data);
        if (!obj) return nullptr;
        auto it = obj->find(key);
        if (it == obj->end()) return nullptr;
        return &it->second;
    }

    [[nodiscard]] auto stringify() const -> std::string {
        return std::visit([](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) {
                return "null";
            } else if constexpr (std::is_same_v<T, bool>) {
                return v ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                return std::to_string(v);
            } else if constexpr (std::is_same_v<T, double>) {
                return std::format("{}", v);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return std::format("\"{}\"", cfire::detail::json_escape(v));
            } else if constexpr (std::is_same_v<T, JsonArray>) {
                std::string out = "[";
                for (std::size_t i = 0; i < v.size(); ++i) {
                    if (i > 0) out += ",";
                    out += v[i].stringify();
                }
                out += "]";
                return out;
            } else {
                std::string out = "{";
                bool first = true;
                for (const auto& [k, val] : v) {
                    if (!first) out += ",";
                    first = false;
                    out += std::format("\"{}\":{}", cfire::detail::json_escape(k), val.stringify());
                }
                out += "}";
                return out;
            }
        }, data);
    }
};

namespace detail {

inline void append_utf8_cp(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

[[nodiscard]] inline auto hex_digit_val(char c) noexcept -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

class JsonParser {
public:
    explicit JsonParser(std::string_view s) : s_(s) {}

    [[nodiscard]] auto parse_root() -> std::optional<JsonVal> {
        skip_ws();
        auto val = parse_value(0);
        if (!val) return std::nullopt;
        skip_ws();
        if (pos_ != s_.size()) return std::nullopt;
        return val;
    }

private:
    std::string_view s_;
    std::size_t pos_ = 0;

    void skip_ws() noexcept {
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    [[nodiscard]] auto parse_hex4(std::uint32_t& out) noexcept -> bool {
        if (pos_ + 4 > s_.size()) return false;
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            int d = hex_digit_val(s_[pos_ + static_cast<std::size_t>(i)]);
            if (d < 0) return false;
            v = (v << 4) | static_cast<std::uint32_t>(d);
        }
        pos_ += 4;
        out = v;
        return true;
    }

    [[nodiscard]] auto parse_string() -> std::optional<std::string> {
        if (pos_ >= s_.size() || s_[pos_] != '"') return std::nullopt;
        ++pos_;
        std::string out;
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) return std::nullopt;
            if (c == '\\') {
                if (pos_ >= s_.size()) return std::nullopt;
                char esc = s_[pos_++];
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        std::uint32_t cp = 0;
                        if (!parse_hex4(cp)) return std::nullopt;
                        if (cp >= 0xD800 && cp <= 0xDBFF &&
                            pos_ + 6 <= s_.size() && s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
                            std::size_t saved = pos_;
                            pos_ += 2;
                            std::uint32_t low = 0;
                            if (parse_hex4(low) && low >= 0xDC00 && low <= 0xDFFF) {
                                cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
                            } else {
                                pos_ = saved;
                            }
                        }
                        append_utf8_cp(out, cp);
                        break;
                    }
                    default: return std::nullopt;
                }
            } else {
                out.push_back(c);
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] auto parse_number() -> std::optional<JsonVal> {
        std::size_t start = pos_;
        bool is_float = false;
        if (pos_ < s_.size() && s_[pos_] == '-') ++pos_;
        if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_]))) return std::nullopt;
        if (s_[pos_] == '0') {
            ++pos_;
        } else {
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        }
        if (pos_ < s_.size() && s_[pos_] == '.') {
            is_float = true;
            ++pos_;
            if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_]))) return std::nullopt;
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            is_float = true;
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
            if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_]))) return std::nullopt;
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        }
        std::string_view slice = s_.substr(start, pos_ - start);
        if (!is_float) {
            std::int64_t iv = 0;
            auto [ptr, ec] = std::from_chars(slice.data(), slice.data() + slice.size(), iv);
            if (ec == std::errc{} && ptr == slice.data() + slice.size()) {
                return JsonVal(iv);
            }
        }
        std::string tmp(slice);
        char* end_p = nullptr;
        double dv = std::strtod(tmp.c_str(), &end_p);
        if (end_p == tmp.c_str() + tmp.size()) {
            return JsonVal(dv);
        }
        return std::nullopt;
    }

    [[nodiscard]] auto parse_value(int depth) -> std::optional<JsonVal> {
        if (depth > 128) return std::nullopt;
        skip_ws();
        if (pos_ >= s_.size()) return std::nullopt;
        char c = s_[pos_];
        if (c == '"') {
            auto str = parse_string();
            if (!str) return std::nullopt;
            return JsonVal(std::move(*str));
        }
        if (c == '{') {
            ++pos_;
            JsonObject obj;
            skip_ws();
            if (pos_ < s_.size() && s_[pos_] == '}') {
                ++pos_;
                return JsonVal(std::move(obj));
            }
            while (true) {
                skip_ws();
                auto key = parse_string();
                if (!key) return std::nullopt;
                skip_ws();
                if (pos_ >= s_.size() || s_[pos_] != ':') return std::nullopt;
                ++pos_;
                auto val = parse_value(depth + 1);
                if (!val) return std::nullopt;
                obj.insert_or_assign(std::move(*key), std::move(*val));
                skip_ws();
                if (pos_ >= s_.size()) return std::nullopt;
                if (s_[pos_] == '}') {
                    ++pos_;
                    return JsonVal(std::move(obj));
                }
                if (s_[pos_] != ',') return std::nullopt;
                ++pos_;
            }
        }
        if (c == '[') {
            ++pos_;
            JsonArray arr;
            skip_ws();
            if (pos_ < s_.size() && s_[pos_] == ']') {
                ++pos_;
                return JsonVal(std::move(arr));
            }
            while (true) {
                auto val = parse_value(depth + 1);
                if (!val) return std::nullopt;
                arr.push_back(std::move(*val));
                skip_ws();
                if (pos_ >= s_.size()) return std::nullopt;
                if (s_[pos_] == ']') {
                    ++pos_;
                    return JsonVal(std::move(arr));
                }
                if (s_[pos_] != ',') return std::nullopt;
                ++pos_;
            }
        }
        if (s_.substr(pos_).starts_with("true")) {
            pos_ += 4;
            return JsonVal(true);
        }
        if (s_.substr(pos_).starts_with("false")) {
            pos_ += 5;
            return JsonVal(false);
        }
        if (s_.substr(pos_).starts_with("null")) {
            pos_ += 4;
            return JsonVal(nullptr);
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            return parse_number();
        }
        return std::nullopt;
    }
};

template <typename T>
struct is_std_expected : std::false_type {};
template <typename V, typename E>
struct is_std_expected<std::expected<V, E>> : std::true_type {};

template <typename R>
[[nodiscard]] inline auto serialize_tool_return(R&& val) -> std::expected<std::string, cfire::Error> {
    using DecayR = std::decay_t<R>;
    if constexpr (std::is_same_v<DecayR, std::string>) {
        return std::forward<R>(val);
    } else if constexpr (std::is_convertible_v<DecayR, std::string_view>) {
        return std::string(std::string_view(val));
    } else if constexpr (is_std_expected<DecayR>::value) {
        if (!val) return std::unexpected(val.error());
        using ValT = std::decay_t<typename DecayR::value_type>;
        if constexpr (std::is_void_v<ValT>) {
            return std::string("ok");
        } else {
            return serialize_tool_return(*std::forward<R>(val));
        }
    } else {
        return std::string{};
    }
}

} // namespace detail

[[nodiscard]] inline auto parse_json_value(std::string_view text) -> std::optional<JsonVal> {
    return detail::JsonParser(text).parse_root();
}

[[nodiscard]] inline auto get_req_str(const JsonVal& obj, std::string_view key)
    -> std::expected<std::string, cfire::Error>
{
    const JsonVal* v = obj.get(key);
    if (!v || !v->is_string()) {
        return cfire::make_error(std::format("missing or invalid string field '{}'", key));
    }
    return std::string(v->as_str());
}

[[nodiscard]] inline auto get_opt_str(const JsonVal& obj, std::string_view key)
    -> std::expected<std::optional<std::string>, cfire::Error>
{
    const JsonVal* v = obj.get(key);
    if (!v || v->is_null()) return std::optional<std::string>{};
    if (!v->is_string()) {
        return cfire::make_error(std::format("field '{}' must be a string", key));
    }
    return std::optional<std::string>{std::string(v->as_str())};
}

[[nodiscard]] inline auto get_opt_bool(const JsonVal& obj, std::string_view key)
    -> std::expected<std::optional<bool>, cfire::Error>
{
    const JsonVal* v = obj.get(key);
    if (!v || v->is_null()) return std::optional<bool>{};
    if (const auto* b = std::get_if<bool>(&v->data)) {
        return std::optional<bool>{*b};
    }
    return cfire::make_error(std::format("field '{}' must be a boolean", key));
}

[[nodiscard]] inline auto get_opt_i64(const JsonVal& obj, std::string_view key)
    -> std::expected<std::optional<std::int64_t>, cfire::Error>
{
    const JsonVal* v = obj.get(key);
    if (!v || v->is_null()) return std::optional<std::int64_t>{};
    if (const auto* iv = std::get_if<std::int64_t>(&v->data)) {
        return std::optional<std::int64_t>{*iv};
    }
    return cfire::make_error(std::format("field '{}' must be an integer", key));
}

template <typename T>
[[nodiscard]] inline auto schema_for() -> std::string;

template <typename T>
[[nodiscard]] inline auto parse_input(const JsonVal& obj) -> std::expected<T, cfire::Error>;

struct ToolSpec {
    std::string name;
    std::string description;
    std::string schema_json;
};

struct RegisteredTool {
    ToolSpec spec;
    std::function<std::expected<std::string, cfire::Error>(const JsonVal&)> handler;
};

class McpServer {
public:
    McpServer() = default;
    explicit McpServer(std::string name, std::string version = "1.0.0")
        : name_(std::move(name)), version_(std::move(version)) {}

    auto add_tool(
        std::string name,
        std::string description,
        std::string schema_json,
        std::function<std::expected<std::string, cfire::Error>(const JsonVal&)> handler
    ) -> void {
        tools_.push_back(RegisteredTool{
            ToolSpec{std::move(name), std::move(description), std::move(schema_json)},
            std::move(handler)
        });
    }

    template <typename ArgsStruct, typename Fn>
    auto register_tool(std::string name, std::string description, Fn&& fn) -> void {
        std::string schema = schema_for<ArgsStruct>();
        add_tool(
            std::move(name),
            std::move(description),
            std::move(schema),
            [func = std::forward<Fn>(fn)](const JsonVal& args_obj) -> std::expected<std::string, cfire::Error> {
                auto parsed = parse_input<ArgsStruct>(args_obj);
                if (!parsed) return std::unexpected(parsed.error());
                return detail::serialize_tool_return(func(*parsed));
            }
        );
    }

    template <typename Fn>
    auto register_tool_action(std::string name, std::string description, Fn&& fn) -> void {
        add_tool(
            std::move(name),
            std::move(description),
            R"({"type":"object","properties":{},"additionalProperties":false})",
            [func = std::forward<Fn>(fn)](const JsonVal&) -> std::expected<std::string, cfire::Error> {
                return detail::serialize_tool_return(func());
            }
        );
    }

    [[nodiscard]] auto tools() const noexcept -> const std::vector<RegisteredTool>& {
        return tools_;
    }

    [[nodiscard]] auto handle_jsonrpc(std::string_view line) const -> std::string {
        auto req = parse_json_value(line);
        if (!req || !req->is_object()) {
            return R"({"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"Parse error"}})";
        }

        std::string id_raw = "null";
        if (const JsonVal* id_val = req->get("id")) {
            id_raw = id_val->stringify();
        }

        const JsonVal* m_val = req->get("method");
        if (!m_val || !m_val->is_string()) {
            return std::format(R"({{"jsonrpc":"2.0","id":{},"error":{{"code":-32600,"message":"Invalid Request"}}}})", id_raw);
        }
        std::string_view method = m_val->as_str();

        if (method.starts_with("notifications/")) {
            return "";
        }

        if (method == "initialize") {
            return std::format(
                R"({{"jsonrpc":"2.0","id":{},"result":{{"protocolVersion":"2024-11-05","capabilities":{{"tools":{{}}}},"serverInfo":{{"name":"{}","version":"{}"}}}}}})",
                id_raw,
                cfire::detail::json_escape(name_),
                cfire::detail::json_escape(version_));
        }

        if (method == "ping") {
            return std::format(R"({{"jsonrpc":"2.0","id":{},"result":{{}}}})", id_raw);
        }

        if (method == "tools/list") {
            std::string list_json = "[";
            for (std::size_t i = 0; i < tools_.size(); ++i) {
                if (i > 0) list_json += ",";
                list_json += std::format(
                    R"({{"name":"{}","description":"{}","inputSchema":{}}})",
                    cfire::detail::json_escape(tools_[i].spec.name),
                    cfire::detail::json_escape(tools_[i].spec.description),
                    tools_[i].spec.schema_json);
            }
            list_json += "]";
            return std::format(R"({{"jsonrpc":"2.0","id":{},"result":{{"tools":{}}}}})", id_raw, list_json);
        }

        if (method == "tools/call") {
            const JsonVal* params = req->get("params");
            if (!params || !params->is_object()) {
                return std::format(R"({{"jsonrpc":"2.0","id":{},"error":{{"code":-32602,"message":"Missing params"}}}})", id_raw);
            }
            const JsonVal* tname = params->get("name");
            if (!tname || !tname->is_string()) {
                return std::format(R"({{"jsonrpc":"2.0","id":{},"error":{{"code":-32602,"message":"Missing tool name"}}}})", id_raw);
            }
            JsonVal empty_args{JsonObject{}};
            const JsonVal* args_ptr = &empty_args;
            if (const JsonVal* a = params->get("arguments")) {
                if (a->is_object()) {
                    args_ptr = a;
                } else if (!a->is_null()) {
                    return std::format(
                        R"({{"jsonrpc":"2.0","id":{},"result":{{"content":[{{"type":"text","text":"tool arguments must be a JSON object"}}],"isError":true}}}})",
                        id_raw);
                }
            }
            for (const auto& t : tools_) {
                if (t.spec.name == tname->as_str()) {
                    std::expected<std::string, cfire::Error> res;
                    try {
                        res = t.handler(*args_ptr);
                    } catch (const std::exception& e) {
                        res = std::unexpected(cfire::Error(std::format("internal error in tool '{}': {}", t.spec.name, e.what())));
                    } catch (...) {
                        res = std::unexpected(cfire::Error(std::format("internal error in tool '{}': unknown exception", t.spec.name)));
                    }
                    if (!res) {
                        return std::format(
                            R"({{"jsonrpc":"2.0","id":{},"result":{{"content":[{{"type":"text","text":"{}"}}],"isError":true}}}})",
                            id_raw,
                            cfire::detail::json_escape(res.error().message()));
                    }
                    return std::format(
                        R"({{"jsonrpc":"2.0","id":{},"result":{{"content":[{{"type":"text","text":"{}"}}],"isError":false}}}})",
                        id_raw,
                        cfire::detail::json_escape(*res));
                }
            }
            return std::format(R"({{"jsonrpc":"2.0","id":{},"error":{{"code":-32601,"message":"Unknown tool"}}}})", id_raw);
        }

        return std::format(R"({{"jsonrpc":"2.0","id":{},"error":{{"code":-32601,"message":"Method not found"}}}})", id_raw);
    }

    auto serve_stdio() const -> int {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty()) continue;
            std::string reply = handle_jsonrpc(line);
            if (!reply.empty()) {
                std::cout << reply << "\n" << std::flush;
            }
        }
        return 0;
    }

private:
    std::string name_{"cfire-mcp"};
    std::string version_{"1.0.0"};
    std::vector<RegisteredTool> tools_;
};

[[nodiscard]] inline auto make_mcp_server(std::string name, std::string version = "1.0.0") -> McpServer {
    return McpServer(std::move(name), std::move(version));
}

} // namespace cfire::mcp
