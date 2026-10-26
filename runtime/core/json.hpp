#pragma once
#include "../cfire_prelude.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <expected>
#include <sstream>
#include <cctype>
#include <charconv>

namespace cfire::json {

[[nodiscard]] inline auto escape_string(std::string_view sv) -> std::string {
    std::string out;
    out.reserve(sv.size() + 16);
    for (char c : sv) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += std::format("\\u{:04x}", static_cast<unsigned int>(static_cast<unsigned char>(c)));
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    return out;
}

struct Value;
using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;

struct Value {
    using VariantType = std::variant<std::monostate, bool, int64_t, double, std::string, Array, Object>;
    VariantType data{};

    Value() : data(std::monostate{}) {}
    Value(std::nullptr_t) : data(std::monostate{}) {}
    Value(bool b) : data(b) {}
    Value(int v) : data(static_cast<int64_t>(v)) {}
    Value(int64_t v) : data(v) {}
    Value(uint64_t v) : data(static_cast<int64_t>(v)) {}
    Value(double v) : data(v) {}
    Value(std::string s) : data(std::move(s)) {}
    Value(std::string_view s) : data(std::string(s)) {}
    Value(const char* s) : data(std::string(s)) {}
    Value(Array a) : data(std::move(a)) {}
    Value(Object o) : data(std::move(o)) {}

    [[nodiscard]] auto is_null() const noexcept -> bool { return std::holds_alternative<std::monostate>(data); }
    [[nodiscard]] auto is_bool() const noexcept -> bool { return std::holds_alternative<bool>(data); }
    [[nodiscard]] auto is_int() const noexcept -> bool { return std::holds_alternative<int64_t>(data); }
    [[nodiscard]] auto is_float() const noexcept -> bool { return std::holds_alternative<double>(data); }
    [[nodiscard]] auto is_number() const noexcept -> bool { return is_int() || is_float(); }
    [[nodiscard]] auto is_string() const noexcept -> bool { return std::holds_alternative<std::string>(data); }
    [[nodiscard]] auto is_array() const noexcept -> bool { return std::holds_alternative<Array>(data); }
    [[nodiscard]] auto is_object() const noexcept -> bool { return std::holds_alternative<Object>(data); }

    [[nodiscard]] auto as_bool() const noexcept -> bool {
        if (const auto* b = std::get_if<bool>(&data)) return *b;
        return false;
    }

    [[nodiscard]] auto as_int() const noexcept -> int64_t {
        if (const auto* i = std::get_if<int64_t>(&data)) return *i;
        if (const auto* d = std::get_if<double>(&data)) return static_cast<int64_t>(*d);
        return 0;
    }

    [[nodiscard]] auto as_float() const noexcept -> double {
        if (const auto* d = std::get_if<double>(&data)) return *d;
        if (const auto* i = std::get_if<int64_t>(&data)) return static_cast<double>(*i);
        return 0.0;
    }

    [[nodiscard]] auto as_str() const noexcept -> std::string_view {
        if (const auto* s = std::get_if<std::string>(&data)) return *s;
        return "";
    }

    [[nodiscard]] auto as_array() const -> const Array& {
        static const Array empty_arr;
        if (const auto* a = std::get_if<Array>(&data)) return *a;
        return empty_arr;
    }

    [[nodiscard]] auto as_object() const -> const Object& {
        static const Object empty_obj;
        if (const auto* o = std::get_if<Object>(&data)) return *o;
        return empty_obj;
    }

    [[nodiscard]] auto get(std::string_view key) const -> std::optional<Value> {
        if (const auto* o = std::get_if<Object>(&data)) {
            for (const auto& [k, v] : *o) {
                if (k == key) return v;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] auto at(std::size_t index) const -> std::optional<Value> {
        if (const auto* a = std::get_if<Array>(&data)) {
            if (index < a->size()) return (*a)[index];
        }
        return std::nullopt;
    }

    [[nodiscard]] auto operator[](std::string_view key) const -> Value {
        if (auto v = get(key)) return *v;
        return Value{};
    }

    [[nodiscard]] auto operator[](std::size_t index) const -> Value {
        if (auto v = at(index)) return *v;
        return Value{};
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t {
        if (const auto* a = std::get_if<Array>(&data)) return a->size();
        if (const auto* o = std::get_if<Object>(&data)) return o->size();
        if (const auto* s = std::get_if<std::string>(&data)) return s->size();
        return 0;
    }

    [[nodiscard]] auto keys() const -> std::vector<std::string> {
        std::vector<std::string> res;
        if (const auto* o = std::get_if<Object>(&data)) {
            for (const auto& [k, _] : *o) res.push_back(k);
        }
        return res;
    }
};

class DynamicJsonParser {
public:
    explicit DynamicJsonParser(std::string_view s) : src_(s) {}

    auto parse() -> std::expected<Value, cfire::Error> {
        skip_ws();
        auto val = parse_val();
        if (!val) return std::unexpected(val.error());
        skip_ws();
        if (pos_ < src_.size()) {
            return cfire::make_error(std::format("unexpected trailing characters at position {}", pos_));
        }
        return val;
    }

private:
    static constexpr int kMaxJsonDepth = 128;
    std::string_view src_;
    std::size_t pos_ = 0;
    int depth_ = 0;

    struct DepthGuard {
        int& d;
        explicit DepthGuard(int& depth) noexcept : d(depth) { ++d; }
        ~DepthGuard() { --d; }
    };

    void skip_ws() {
        while (pos_ < src_.size() && (src_[pos_] == ' ' || src_[pos_] == '\t' || src_[pos_] == '\n' || src_[pos_] == '\r')) {
            ++pos_;
        }
    }

    char cur() const { return pos_ < src_.size() ? src_[pos_] : '\0'; }

    auto parse_val() -> std::expected<Value, cfire::Error> {
        if (depth_ >= kMaxJsonDepth) {
            return cfire::make_error("maximum JSON nesting depth (128) exceeded");
        }
        DepthGuard guard(depth_);
        skip_ws();
        char c = cur();
        if (c == '{') return parse_obj();
        if (c == '[') return parse_arr();
        if (c == '"') return parse_str();
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') return parse_null();
        if (c == '-' || (c >= '0' && c <= '9')) return parse_num();
        return cfire::make_error(std::format("unexpected token '{}' at position {}", c, pos_));
    }

    auto parse_obj() -> std::expected<Value, cfire::Error> {
        ++pos_; // '{'
        skip_ws();
        Object obj;
        if (cur() == '}') {
            ++pos_;
            return Value(std::move(obj));
        }
        while (pos_ < src_.size()) {
            skip_ws();
            if (cur() != '"') return cfire::make_error(std::format("expected string key in object at position {}", pos_));
            auto key_val = parse_str();
            if (!key_val) return std::unexpected(key_val.error());
            std::string key = std::string(key_val->as_str());
            skip_ws();
            if (cur() != ':') return cfire::make_error(std::format("expected ':' after key in object at position {}", pos_));
            ++pos_; // ':'
            skip_ws();
            auto v = parse_val();
            if (!v) return std::unexpected(v.error());
            obj.emplace_back(std::move(key), std::move(*v));
            skip_ws();
            if (cur() == '}') {
                ++pos_;
                return Value(std::move(obj));
            }
            if (cur() == ',') {
                ++pos_;
            } else {
                return cfire::make_error(std::format("expected ',' or '}}' in object at position {}", pos_));
            }
        }
        return cfire::make_error("unterminated object");
    }

    auto parse_arr() -> std::expected<Value, cfire::Error> {
        ++pos_; // '['
        skip_ws();
        Array arr;
        if (cur() == ']') {
            ++pos_;
            return Value(std::move(arr));
        }
        while (pos_ < src_.size()) {
            skip_ws();
            auto v = parse_val();
            if (!v) return std::unexpected(v.error());
            arr.push_back(std::move(*v));
            skip_ws();
            if (cur() == ']') {
                ++pos_;
                return Value(std::move(arr));
            }
            if (cur() == ',') {
                ++pos_;
            } else {
                return cfire::make_error(std::format("expected ',' or ']' in array at position {}", pos_));
            }
        }
        return cfire::make_error("unterminated array");
    }

    auto parse_str() -> std::expected<Value, cfire::Error> {
        ++pos_; // '"'
        std::string s;
        while (pos_ < src_.size()) {
            char c = src_[pos_++];
            if (c == '"') return Value(std::move(s));
            if (c == '\\') {
                if (pos_ >= src_.size()) return cfire::make_error("unexpected end of string escape");
                char esc = src_[pos_++];
                switch (esc) {
                    case '"': s.push_back('"'); break;
                    case '\\': s.push_back('\\'); break;
                    case '/': s.push_back('/'); break;
                    case 'b': s.push_back('\b'); break;
                    case 'f': s.push_back('\f'); break;
                    case 'n': s.push_back('\n'); break;
                    case 'r': s.push_back('\r'); break;
                    case 't': s.push_back('\t'); break;
                    case 'u': {
                        if (pos_ + 4 > src_.size()) return cfire::make_error("incomplete unicode escape");
                        std::string hex_str(src_.substr(pos_, 4));
                        pos_ += 4;
                        unsigned int cp = 0;
                        auto [ptr, ec] = std::from_chars(hex_str.data(), hex_str.data() + 4, cp, 16);
                        if (ec != std::errc()) return cfire::make_error("invalid unicode escape");
                        if (cp < 0x80) {
                            s.push_back(static_cast<char>(cp));
                        } else if (cp < 0x800) {
                            s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        } else {
                            s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                            s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        break;
                    }
                    default:
                        s.push_back(esc);
                        break;
                }
            } else {
                s.push_back(c);
            }
        }
        return cfire::make_error("unterminated string");
    }

    auto parse_bool() -> std::expected<Value, cfire::Error> {
        if (src_.substr(pos_).starts_with("true")) {
            pos_ += 4;
            return Value(true);
        }
        if (src_.substr(pos_).starts_with("false")) {
            pos_ += 5;
            return Value(false);
        }
        return cfire::make_error(std::format("invalid boolean at position {}", pos_));
    }

    auto parse_null() -> std::expected<Value, cfire::Error> {
        if (src_.substr(pos_).starts_with("null")) {
            pos_ += 4;
            return Value{};
        }
        return cfire::make_error(std::format("invalid null at position {}", pos_));
    }

    auto parse_num() -> std::expected<Value, cfire::Error> {
        std::size_t start = pos_;
        bool is_float = false;
        if (cur() == '-') ++pos_;
        while (pos_ < src_.size() && (src_[pos_] >= '0' && src_[pos_] <= '9')) ++pos_;
        if (pos_ < src_.size() && src_[pos_] == '.') {
            is_float = true;
            ++pos_;
            while (pos_ < src_.size() && (src_[pos_] >= '0' && src_[pos_] <= '9')) ++pos_;
        }
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            is_float = true;
            ++pos_;
            if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) ++pos_;
            while (pos_ < src_.size() && (src_[pos_] >= '0' && src_[pos_] <= '9')) ++pos_;
        }
        std::string_view num_str = src_.substr(start, pos_ - start);
        if (is_float) {
            char* endptr = nullptr;
            std::string s(num_str);
            double d = std::strtod(s.c_str(), &endptr);
            return Value(d);
        } else {
            int64_t i = 0;
            auto [p, ec] = std::from_chars(num_str.data(), num_str.data() + num_str.size(), i);
            if (ec != std::errc()) {
                return cfire::make_error(std::format("invalid integer number '{}'", num_str));
            }
            return Value(i);
        }
    }
};

[[nodiscard]] inline auto parse_dynamic(std::string_view payload) -> std::expected<Value, cfire::Error> {
    DynamicJsonParser p(payload);
    return p.parse();
}

inline void stringify_value_to(const Value& val, std::string& out, int indent, int current_depth) {
    if (val.is_null()) {
        out += "null";
    } else if (val.is_bool()) {
        out += val.as_bool() ? "true" : "false";
    } else if (val.is_int()) {
        out += std::to_string(val.as_int());
    } else if (val.is_float()) {
        out += std::format("{}", val.as_float());
    } else if (val.is_string()) {
        out += "\"";
        out += escape_string(val.as_str());
        out += "\"";
    } else if (val.is_array()) {
        const auto& arr = val.as_array();
        if (arr.empty()) {
            out += "[]";
            return;
        }
        out += "[";
        if (indent > 0) out += "\n";
        for (std::size_t i = 0; i < arr.size(); ++i) {
            if (indent > 0) out.append((current_depth + 1) * indent, ' ');
            stringify_value_to(arr[i], out, indent, current_depth + 1);
            if (i + 1 < arr.size()) out += ",";
            if (indent > 0) out += "\n";
        }
        if (indent > 0) out.append(current_depth * indent, ' ');
        out += "]";
    } else if (val.is_object()) {
        const auto& obj = val.as_object();
        if (obj.empty()) {
            out += "{}";
            return;
        }
        out += "{";
        if (indent > 0) out += "\n";
        for (std::size_t i = 0; i < obj.size(); ++i) {
            if (indent > 0) out.append((current_depth + 1) * indent, ' ');
            out += "\"";
            out += escape_string(obj[i].first);
            out += "\":";
            if (indent > 0) out += " ";
            stringify_value_to(obj[i].second, out, indent, current_depth + 1);
            if (i + 1 < obj.size()) out += ",";
            if (indent > 0) out += "\n";
        }
        if (indent > 0) out.append(current_depth * indent, ' ');
        out += "}";
    }
}

[[nodiscard]] inline auto stringify_dynamic(const Value& val, int indent = 0) -> std::string {
    std::string out;
    stringify_value_to(val, out, indent, 0);
    return out;
}

} // namespace cfire::json

#ifdef CFIRE_USE_SIMDJSON
#if defined(PLATFORM_WEB) || defined(PLATFORM_IOS) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
namespace simdjson::ondemand {
    struct object {
        const cfire::json::Value* val = nullptr;
    };
}
#else
#include <simdjson.h>
#endif
namespace cfire::json {

template <typename T>
auto parse_struct(simdjson::ondemand::object) -> std::expected<T, cfire::Error> {
    return T{};
}

template <typename T>
auto stringify_struct(const T&) -> std::string {
    return "{}";
}

namespace detail {
template <typename T>
struct is_std_vector : std::false_type {};
template <typename U, typename A>
struct is_std_vector<std::vector<U, A>> : std::true_type {};
} // namespace detail

template <typename T>
[[nodiscard]] auto parse(std::string_view payload) -> std::expected<T, cfire::Error> {
#if defined(PLATFORM_WEB) || defined(PLATFORM_IOS) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    if constexpr (std::is_same_v<T, std::string>) {
        if (payload.size() >= 2 && payload.front() == '"' && payload.back() == '"') {
            auto dyn = parse_dynamic(payload);
            if (dyn && dyn->is_string()) return std::string(dyn->as_str());
            return std::string(payload.substr(1, payload.size() - 2));
        }
        return std::string(payload);
    } else if constexpr (detail::is_std_vector<T>::value) {
        using Elem = typename T::value_type;
        auto dyn = parse_dynamic(payload);
        if (!dyn) return std::unexpected(dyn.error());
        if (!dyn->is_array()) return cfire::make_error("expected json array");
        T out;
        for (const auto& elem : dyn->as_array()) {
            if constexpr (std::is_same_v<Elem, std::string>) {
                if (!elem.is_string()) return cfire::make_error("invalid string in json array");
                out.emplace_back(elem.as_str());
            } else {
                if (!elem.is_object()) return cfire::make_error("invalid object in json array");
                simdjson::ondemand::object obj{&elem};
                auto parsed = parse_struct<Elem>(obj);
                if (!parsed) return std::unexpected(parsed.error());
                out.push_back(std::move(*parsed));
            }
        }
        return out;
    } else {
        auto dyn = parse_dynamic(payload);
        if (!dyn) return std::unexpected(dyn.error());
        if (!dyn->is_object()) return cfire::make_error("expected json object");
        simdjson::ondemand::object obj{&(*dyn)};
        return parse_struct<T>(obj);
    }
#else
    if constexpr (std::is_same_v<T, std::string>) {
        if (payload.size() >= 2 && payload.front() == '"' && payload.back() == '"') {
            auto dyn = parse_dynamic(payload);
            if (dyn && dyn->is_string()) return std::string(dyn->as_str());
            return std::string(payload.substr(1, payload.size() - 2));
        }
        return std::string(payload);
    } else if constexpr (detail::is_std_vector<T>::value) {
        using Elem = typename T::value_type;
        simdjson::ondemand::parser parser;
        simdjson::padded_string padded(payload.data(), payload.size());
        auto doc = parser.iterate(padded);
        if (doc.error()) {
            return cfire::make_error(std::format("simdjson iterate error: {}", simdjson::error_message(doc.error())));
        }
        auto arr = doc.get_array();
        if (arr.error()) {
            return cfire::make_error(std::format("simdjson array error: {}", simdjson::error_message(arr.error())));
        }
        T out;
        for (auto elem : arr.value()) {
            if constexpr (std::is_same_v<Elem, std::string>) {
                auto sv = elem.get_string();
                if (sv.error()) return cfire::make_error("invalid string in json array");
                out.emplace_back(sv.value());
            } else {
                auto obj = elem.get_object();
                if (obj.error()) return cfire::make_error("invalid object in json array");
                auto parsed = parse_struct<Elem>(obj.value());
                if (!parsed) return std::unexpected(parsed.error());
                out.push_back(std::move(*parsed));
            }
        }
        return out;
    } else {
        simdjson::ondemand::parser parser;
        simdjson::padded_string padded(payload.data(), payload.size());
        auto doc = parser.iterate(padded);
        if (doc.error()) {
            return cfire::make_error(std::format("simdjson iterate error: {}", simdjson::error_message(doc.error())));
        }
        auto obj = doc.get_object();
        if (obj.error()) {
            return cfire::make_error(std::format("simdjson object error: {}", simdjson::error_message(obj.error())));
        }
        return parse_struct<T>(obj.value());
    }
#endif
}

template <typename T>
[[nodiscard]] auto stringify(const T& val) -> std::string {
    using D = std::decay_t<T>;
    if constexpr (std::is_same_v<D, std::string> || std::is_same_v<D, std::string_view> || std::is_same_v<D, const char*>) {
        return std::format("\"{}\"", escape_string(val));
    } else if constexpr (std::is_same_v<D, bool>) {
        return val ? "true" : "false";
    } else if constexpr (std::is_arithmetic_v<D>) {
        return std::format("{}", val);
    } else if constexpr (detail::is_std_vector<D>::value) {
        std::string out = "[";
        for (std::size_t i = 0; i < val.size(); ++i) {
            if (i > 0) out += ",";
            out += stringify(val[i]);
        }
        out += "]";
        return out;
    } else {
        return stringify_struct<D>(val);
    }
}

#if !defined(PLATFORM_WEB) && !defined(PLATFORM_IOS) && !(defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
[[nodiscard]] inline auto get_str(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<std::string, cfire::Error>
{
    auto res = obj[key].get_string();
    if (res.error()) {
        return cfire::make_error(std::format("missing or invalid string field '{}'", key));
    }
    return std::string(res.value());
}

[[nodiscard]] inline auto get_u64(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<std::uint64_t, cfire::Error>
{
    auto res = obj[key].get_uint64();
    if (res.error()) {
        return cfire::make_error(std::format("missing or invalid u64 field '{}'", key));
    }
    return res.value();
}

[[nodiscard]] inline auto get_i64(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<std::int64_t, cfire::Error>
{
    auto res = obj[key].get_int64();
    if (res.error()) {
        return cfire::make_error(std::format("missing or invalid i64 field '{}'", key));
    }
    return res.value();
}

[[nodiscard]] inline auto get_i32(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<std::int32_t, cfire::Error>
{
    auto res = obj[key].get_int64();
    if (res.error()) {
        return cfire::make_error(std::format("missing or invalid i32 field '{}'", key));
    }
    return static_cast<std::int32_t>(res.value());
}

[[nodiscard]] inline auto get_f64(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<double, cfire::Error>
{
    auto res = obj[key].get_double();
    if (res.error()) {
        return cfire::make_error(std::format("missing or invalid f64 field '{}'", key));
    }
    return res.value();
}

[[nodiscard]] inline auto get_bool(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<bool, cfire::Error>
{
    auto res = obj[key].get_bool();
    if (res.error()) {
        return cfire::make_error(std::format("missing or invalid bool field '{}'", key));
    }
    return res.value();
}
#else
[[nodiscard]] inline auto get_str(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<std::string, cfire::Error> {
    if (!obj.val) return cfire::make_error(std::format("missing string field '{}'", key));
    auto v = obj.val->get(key);
    if (!v || !v->is_string()) return cfire::make_error(std::format("missing or invalid string field '{}'", key));
    return std::string(v->as_str());
}
[[nodiscard]] inline auto get_u64(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<std::uint64_t, cfire::Error> {
    if (!obj.val) return cfire::make_error(std::format("missing u64 field '{}'", key));
    auto v = obj.val->get(key);
    if (!v || !v->is_number()) return cfire::make_error(std::format("missing or invalid u64 field '{}'", key));
    return static_cast<std::uint64_t>(v->as_int());
}
[[nodiscard]] inline auto get_i64(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<std::int64_t, cfire::Error> {
    if (!obj.val) return cfire::make_error(std::format("missing i64 field '{}'", key));
    auto v = obj.val->get(key);
    if (!v || !v->is_number()) return cfire::make_error(std::format("missing or invalid i64 field '{}'", key));
    return v->as_int();
}
[[nodiscard]] inline auto get_i32(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<std::int32_t, cfire::Error> {
    if (!obj.val) return cfire::make_error(std::format("missing i32 field '{}'", key));
    auto v = obj.val->get(key);
    if (!v || !v->is_number()) return cfire::make_error(std::format("missing or invalid i32 field '{}'", key));
    return static_cast<std::int32_t>(v->as_int());
}
[[nodiscard]] inline auto get_f64(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<double, cfire::Error> {
    if (!obj.val) return cfire::make_error(std::format("missing f64 field '{}'", key));
    auto v = obj.val->get(key);
    if (!v || !v->is_number()) return cfire::make_error(std::format("missing or invalid f64 field '{}'", key));
    return v->as_float();
}
[[nodiscard]] inline auto get_bool(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<bool, cfire::Error> {
    if (!obj.val) return cfire::make_error(std::format("missing bool field '{}'", key));
    auto v = obj.val->get(key);
    if (!v || !v->is_bool()) return cfire::make_error(std::format("missing or invalid bool field '{}'", key));
    return v->as_bool();
}
#endif

} // namespace cfire::json
#endif

