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
#include <limits>

namespace cfire::json {

[[nodiscard]] inline auto escape_string(std::string_view sv) -> std::string {
    return cfire::detail::json_escape(sv);
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

    [[nodiscard]] auto operator[](std::string_view key) const -> const Value& {
        static const Value null_val{};
        if (const auto* o = std::get_if<Object>(&data)) {
            for (const auto& [k, v] : *o) {
                if (k == key) return v;
            }
        }
        return null_val;
    }

    [[nodiscard]] auto operator[](std::size_t index) const -> const Value& {
        static const Value null_val{};
        if (const auto* a = std::get_if<Array>(&data)) {
            if (index < a->size()) return (*a)[index];
        }
        return null_val;
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

    [[nodiscard]] auto operator==(const Value& rhs) const noexcept -> bool;
};

[[nodiscard]] inline auto Value::operator==(const Value& rhs) const noexcept -> bool {
    return data == rhs.data;
}

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

template <>
struct std::formatter<cfire::json::Value> : std::formatter<std::string> {
    auto format(const cfire::json::Value& v, std::format_context& ctx) const {
        return std::formatter<std::string>::format(cfire::json::stringify_dynamic(v), ctx);
    }
};

#if defined(CFIRE_USE_SIMDJSON) && __has_include(<simdjson.h>)
#define CFIRE_SIMDJSON_ACTIVE 1
#include <simdjson.h>
#else
#define CFIRE_SIMDJSON_ACTIVE 0
namespace simdjson::ondemand {
    struct object {
        const cfire::json::Value* val = nullptr;
    };
}
#endif
namespace cfire::json {

namespace detail {
template <typename T>
inline constexpr bool always_false_v = false;

template <typename T>
struct is_std_vector : std::false_type {};
template <typename U, typename A>
struct is_std_vector<std::vector<U, A>> : std::true_type {};

template <typename T>
struct is_std_optional : std::false_type {};
template <typename U>
struct is_std_optional<std::optional<U>> : std::true_type {};

template <typename T>
[[nodiscard]] inline auto check_signed_range(std::int64_t v, std::string_view key, std::string_view type_label)
    -> std::expected<T, cfire::Error>
{
    if (v < static_cast<std::int64_t>(std::numeric_limits<T>::min()) ||
        v > static_cast<std::int64_t>(std::numeric_limits<T>::max())) {
        if (key.empty()) {
            return cfire::make_error(std::format("integer value {} out of range for {}", v, type_label));
        }
        return cfire::make_error(std::format("integer value {} out of range for {} field '{}'", v, type_label, key));
    }
    return static_cast<T>(v);
}

template <typename T>
[[nodiscard]] inline auto check_unsigned_range(std::uint64_t v, std::string_view key, std::string_view type_label)
    -> std::expected<T, cfire::Error>
{
    if (v > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
        if (key.empty()) {
            return cfire::make_error(std::format("unsigned value {} out of range for {}", v, type_label));
        }
        return cfire::make_error(std::format("unsigned value {} out of range for {} field '{}'", v, type_label, key));
    }
    return static_cast<T>(v);
}
} // namespace detail

template <typename T>
auto parse_struct(simdjson::ondemand::object) -> std::expected<T, cfire::Error> {
    static_assert(detail::always_false_v<T>,
        "cfire::json::parse_struct<T>: missing generated JSON reflection specialization for type");
    return cfire::make_error("unsupported JSON struct type");
}

template <typename T>
auto stringify_struct(const T&) -> std::string {
    static_assert(detail::always_false_v<T>,
        "cfire::json::stringify_struct<T>: missing generated JSON reflection specialization for type");
    return "{}";
}

template <typename T>
auto schema_struct() -> std::string {
    static_assert(detail::always_false_v<T>,
        "cfire::json::schema_struct<T>: missing generated JSON reflection specialization for type");
    return R"({"type":"object","properties":{},"additionalProperties":false})";
}

template <typename T>
[[nodiscard]] auto schema() -> std::string {
    using D = std::decay_t<T>;
    if constexpr (std::is_same_v<D, Value>) {
        return R"({})";
    } else if constexpr (detail::is_std_optional<D>::value) {
        return schema<typename D::value_type>();
    } else if constexpr (cfire::reflect_enum<D>::is_enum) {
        return schema_struct<D>();
    } else if constexpr (std::is_convertible_v<const T&, std::string_view> || std::is_same_v<D, std::string> || std::is_same_v<D, std::string_view> || std::is_same_v<D, const char*>) {
        return R"({"type":"string"})";
    } else if constexpr (std::is_same_v<D, bool>) {
        return R"({"type":"boolean"})";
    } else if constexpr (std::is_integral_v<D>) {
        return R"({"type":"integer"})";
    } else if constexpr (std::is_floating_point_v<D>) {
        return R"({"type":"number"})";
    } else if constexpr (detail::is_std_vector<D>::value || cfire::is_span_like<D>::value) {
        using Elem = std::remove_const_t<typename D::value_type>;
        return std::format(R"({{"type":"array","items":{}}})", schema<Elem>());
    } else {
        return schema_struct<D>();
    }
}

[[nodiscard]] inline auto with_description(std::string s, std::string_view desc) -> std::string {
    if (desc.empty() || s.size() < 2 || s.back() != '}') return s;
    s.pop_back();
    s += std::format(R"(,"description":"{}"}})", escape_string(desc));
    return s;
}

[[nodiscard]] inline auto with_schema_meta(std::string s, std::string_view desc, std::string_view extra_props) -> std::string {
    if ((desc.empty() && extra_props.empty()) || s.size() < 2 || s.back() != '}') return s;
    s.pop_back();
    if (!desc.empty()) {
        s += std::format(R"(,"description":"{}")", escape_string(desc));
    }
    if (!extra_props.empty()) {
        s += ",";
        s += extra_props;
    }
    s += "}";
    return s;
}

#if !CFIRE_SIMDJSON_ACTIVE
namespace detail {
template <typename T>
[[nodiscard]] inline auto parse_dynamic_value(const Value& elem, std::string_view key = {})
    -> std::expected<T, cfire::Error>
{
    using D = std::decay_t<T>;
    if constexpr (std::is_same_v<D, Value>) {
        return elem;
    } else if constexpr (detail::is_std_optional<D>::value) {
        if (elem.is_null()) return D{std::nullopt};
        auto inner = parse_dynamic_value<typename D::value_type>(elem, key);
        if (!inner) return std::unexpected(inner.error());
        return D{std::move(*inner)};
    } else if constexpr (cfire::reflect_enum<D>::is_enum) {
        if (!elem.is_string()) return cfire::make_error("expected json string for enum");
        auto parsed = D::from_string(elem.as_str());
        if (!parsed) {
            return cfire::make_error(std::format("invalid enum variant '{}' for enum '{}'", elem.as_str(), cfire::reflect_enum<D>::name));
        }
        return *parsed;
    } else if constexpr (std::is_same_v<D, std::string>) {
        if (!elem.is_string()) return cfire::make_error("expected json string");
        return std::string(elem.as_str());
    } else if constexpr (std::is_same_v<D, bool>) {
        if (!elem.is_bool()) return cfire::make_error("expected json boolean");
        return elem.as_bool();
    } else if constexpr (std::is_unsigned_v<D>) {
        if (!elem.is_number() || elem.as_int() < 0) return cfire::make_error("expected non-negative integer");
        return check_unsigned_range<D>(static_cast<std::uint64_t>(elem.as_int()), key, "unsigned");
    } else if constexpr (std::is_integral_v<D>) {
        if (!elem.is_number()) return cfire::make_error("expected json integer");
        return check_signed_range<D>(elem.as_int(), key, "integer");
    } else if constexpr (std::is_floating_point_v<D>) {
        if (!elem.is_number()) return cfire::make_error("expected json number");
        return static_cast<D>(elem.as_float());
    } else if constexpr (detail::is_std_vector<D>::value) {
        using Elem = typename D::value_type;
        if (!elem.is_array()) return cfire::make_error("expected json array");
        D out;
        for (const auto& sub : elem.as_array()) {
            auto pv = parse_dynamic_value<Elem>(sub, key);
            if (!pv) return std::unexpected(pv.error());
            out.push_back(std::move(*pv));
        }
        return out;
    } else {
        if (!elem.is_object()) return cfire::make_error("expected json object");
        simdjson::ondemand::object sub_obj{&elem};
        return parse_struct<D>(sub_obj);
    }
}
} // namespace detail
#else
namespace detail {
[[nodiscard]] inline auto ondemand_to_dynamic(simdjson::ondemand::value v)
    -> std::expected<Value, cfire::Error>
{
    auto t = v.type();
    if (t.error()) {
        return cfire::make_error(std::format("simdjson type error: {}", simdjson::error_message(t.error())));
    }
    switch (t.value()) {
        case simdjson::ondemand::json_type::null: {
            auto isn = v.is_null();
            if (isn.error()) return cfire::make_error("invalid null value");
            return Value{nullptr};
        }
        case simdjson::ondemand::json_type::boolean: {
            auto b = v.get_bool();
            if (b.error()) return cfire::make_error("invalid boolean value");
            return Value{b.value()};
        }
        case simdjson::ondemand::json_type::string: {
            auto s = v.get_string();
            if (s.error()) return cfire::make_error("invalid string value");
            return Value{std::string(s.value())};
        }
        case simdjson::ondemand::json_type::number: {
            auto num = v.get_number();
            if (num.error()) return cfire::make_error("invalid number value");
            if (num.value().is_double()) return Value{num.value().get_double()};
            if (num.value().is_uint64()) return Value{num.value().get_uint64()};
            return Value{num.value().get_int64()};
        }
        case simdjson::ondemand::json_type::array: {
            auto arr = v.get_array();
            if (arr.error()) return cfire::make_error("invalid array value");
            Array out;
            for (auto el : arr.value()) {
                simdjson::ondemand::value ev;
                if (auto err = el.get(ev); err) return cfire::make_error("invalid array element");
                auto dv = ondemand_to_dynamic(ev);
                if (!dv) return std::unexpected(dv.error());
                out.push_back(std::move(*dv));
            }
            return Value{std::move(out)};
        }
        case simdjson::ondemand::json_type::object: {
            auto obj = v.get_object();
            if (obj.error()) return cfire::make_error("invalid object value");
            Object out;
            for (auto field : obj.value()) {
                auto k = field.unescaped_key();
                if (k.error()) return cfire::make_error("invalid object key");
                simdjson::ondemand::value fv;
                if (auto err = field.value().get(fv); err) return cfire::make_error("invalid object field value");
                auto dv = ondemand_to_dynamic(fv);
                if (!dv) return std::unexpected(dv.error());
                out.emplace_back(std::string(k.value()), std::move(*dv));
            }
            return Value{std::move(out)};
        }
        default:
            return Value{nullptr};
    }
}

template <typename T, typename ValLike>
[[nodiscard]] inline auto parse_ondemand_elem(ValLike&& v, std::string_view key = {})
    -> std::expected<T, cfire::Error>
{
    using D = std::decay_t<T>;
    if constexpr (std::is_same_v<D, Value>) {
        simdjson::ondemand::value dv;
        if (auto err = v.get(dv); err) return cfire::make_error("invalid json value");
        return ondemand_to_dynamic(dv);
    } else if constexpr (detail::is_std_optional<D>::value) {
        auto isn = v.is_null();
        if (!isn.error() && isn.value()) return D{std::nullopt};
        auto inner = parse_ondemand_elem<typename D::value_type>(v, key);
        if (!inner) return std::unexpected(inner.error());
        return D{std::move(*inner)};
    } else if constexpr (cfire::reflect_enum<D>::is_enum) {
        auto sv = v.get_string();
        if (sv.error()) return cfire::make_error("invalid string for enum");
        auto parsed = D::from_string(sv.value());
        if (!parsed) {
            return cfire::make_error(std::format("invalid enum variant '{}' for enum '{}'", std::string_view(sv.value()), cfire::reflect_enum<D>::name));
        }
        return *parsed;
    } else if constexpr (std::is_same_v<D, std::string>) {
        auto sv = v.get_string();
        if (sv.error()) return cfire::make_error("invalid string");
        return std::string(sv.value());
    } else if constexpr (std::is_same_v<D, bool>) {
        auto b = v.get_bool();
        if (b.error()) return cfire::make_error("invalid bool");
        return b.value();
    } else if constexpr (std::is_unsigned_v<D>) {
        auto uv = v.get_uint64();
        if (uv.error()) return cfire::make_error("invalid unsigned integer");
        return check_unsigned_range<D>(uv.value(), key, "unsigned");
    } else if constexpr (std::is_integral_v<D>) {
        auto iv = v.get_int64();
        if (iv.error()) return cfire::make_error("invalid integer");
        return check_signed_range<D>(iv.value(), key, "integer");
    } else if constexpr (std::is_floating_point_v<D>) {
        auto dv = v.get_double();
        if (dv.error()) return cfire::make_error("invalid float");
        return static_cast<D>(dv.value());
    } else if constexpr (detail::is_std_vector<D>::value) {
        using Elem = typename D::value_type;
        auto arr = v.get_array();
        if (arr.error()) return cfire::make_error("invalid nested array");
        D out;
        for (auto el : arr.value()) {
            auto pv = parse_ondemand_elem<Elem>(el, key);
            if (!pv) return std::unexpected(pv.error());
            out.push_back(std::move(*pv));
        }
        return out;
    } else {
        auto sub = v.get_object();
        if (sub.error()) return cfire::make_error("invalid object");
        return parse_struct<D>(sub.value());
    }
}
} // namespace detail
#endif

template <typename T>
[[nodiscard]] auto parse(std::string_view payload) -> std::expected<T, cfire::Error> {
    using D = std::decay_t<T>;
#if !CFIRE_SIMDJSON_ACTIVE
    auto dyn = parse_dynamic(payload);
    if (!dyn) return std::unexpected(dyn.error());
    return detail::parse_dynamic_value<D>(*dyn);
#else
    if constexpr (std::is_same_v<D, Value>) {
        return parse_dynamic(payload);
    } else if constexpr (cfire::reflect_enum<D>::is_enum) {
        std::string_view trimmed = payload;
        while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t' || trimmed.front() == '\r' || trimmed.front() == '\n')) trimmed.remove_prefix(1);
        while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t' || trimmed.back() == '\r' || trimmed.back() == '\n')) trimmed.remove_suffix(1);
        if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
            trimmed.remove_prefix(1);
            trimmed.remove_suffix(1);
        }
        auto parsed = D::from_string(trimmed);
        if (!parsed) {
            return cfire::make_error(std::format("invalid enum variant '{}' for enum '{}'", trimmed, cfire::reflect_enum<D>::name));
        }
        return *parsed;
    } else if constexpr (detail::is_std_optional<D>::value) {
        simdjson::ondemand::parser parser;
        simdjson::padded_string padded(payload.data(), payload.size());
        auto doc = parser.iterate(padded);
        if (doc.error()) {
            return cfire::make_error(std::format("simdjson iterate error: {}", simdjson::error_message(doc.error())));
        }
        return detail::parse_ondemand_elem<D>(doc);
    } else if constexpr (std::is_same_v<D, std::string>) {
        if (payload.size() >= 2 && payload.front() == '"' && payload.back() == '"') {
            auto dyn = parse_dynamic(payload);
            if (dyn && dyn->is_string()) return std::string(dyn->as_str());
            return std::string(payload.substr(1, payload.size() - 2));
        }
        return std::string(payload);
    } else if constexpr (std::is_same_v<D, bool>) {
        simdjson::ondemand::parser parser;
        simdjson::padded_string padded(payload.data(), payload.size());
        auto doc = parser.iterate(padded);
        if (doc.error()) return cfire::make_error(std::format("simdjson iterate error: {}", simdjson::error_message(doc.error())));
        return detail::parse_ondemand_elem<D>(doc);
    } else if constexpr (std::is_arithmetic_v<D>) {
        simdjson::ondemand::parser parser;
        simdjson::padded_string padded(payload.data(), payload.size());
        auto doc = parser.iterate(padded);
        if (doc.error()) return cfire::make_error(std::format("simdjson iterate error: {}", simdjson::error_message(doc.error())));
        return detail::parse_ondemand_elem<D>(doc);
    } else if constexpr (detail::is_std_vector<D>::value) {
        using Elem = typename D::value_type;
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
        D out;
        for (auto elem : arr.value()) {
            auto parsed = detail::parse_ondemand_elem<Elem>(elem);
            if (!parsed) return std::unexpected(parsed.error());
            out.push_back(std::move(*parsed));
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
        return parse_struct<D>(obj.value());
    }
#endif
}

template <typename T>
[[nodiscard]] auto stringify(const T& val) -> std::string {
    using D = std::decay_t<T>;
    if constexpr (std::is_same_v<D, Value>) {
        return stringify_dynamic(val);
    } else if constexpr (detail::is_std_optional<D>::value) {
        return val.has_value() ? stringify(*val) : "null";
    } else if constexpr (cfire::reflect_enum<D>::is_enum) {
        return std::format("\"{}\"", val.to_string());
    } else if constexpr (std::is_convertible_v<const T&, std::string_view> || std::is_same_v<D, std::string> || std::is_same_v<D, std::string_view> || std::is_same_v<D, const char*>) {
        return std::format("\"{}\"", escape_string(std::string_view(val)));
    } else if constexpr (std::is_same_v<D, bool>) {
        return val ? "true" : "false";
    } else if constexpr (std::is_arithmetic_v<D>) {
        return std::format("{}", val);
    } else if constexpr (detail::is_std_vector<D>::value || cfire::is_span_like<D>::value) {
        using Elem = std::remove_const_t<typename D::value_type>;
        std::string out = "[";
        for (std::size_t i = 0; i < val.size(); ++i) {
            if (i > 0) out += ",";
            if constexpr (std::is_same_v<Elem, bool>) {
                out += static_cast<bool>(val[i]) ? "true" : "false";
            } else {
                out += stringify(val[i]);
            }
        }
        out += "]";
        return out;
    } else {
        return stringify_struct<D>(val);
    }
}

#if CFIRE_SIMDJSON_ACTIVE
namespace detail {
template <typename T>
[[nodiscard]] inline auto get_signed_field(
    simdjson::ondemand::object& obj,
    std::string_view key,
    T def,
    std::string_view type_label
) -> std::expected<T, cfire::Error> {
    auto f = obj[key];
    if (f.error() == simdjson::NO_SUCH_FIELD) return def;
    if (!f.error() && f.value().is_null()) return def;
    auto res = f.get_int64();
    if (res.error()) {
        return cfire::make_error(std::format("missing or invalid {} field '{}'", type_label, key));
    }
    return check_signed_range<T>(res.value(), key, type_label);
}

template <typename T>
[[nodiscard]] inline auto get_unsigned_field(
    simdjson::ondemand::object& obj,
    std::string_view key,
    T def,
    std::string_view type_label
) -> std::expected<T, cfire::Error> {
    auto f = obj[key];
    if (f.error() == simdjson::NO_SUCH_FIELD) return def;
    if (!f.error() && f.value().is_null()) return def;
    auto res = f.get_uint64();
    if (res.error()) {
        return cfire::make_error(std::format("missing or invalid {} field '{}'", type_label, key));
    }
    return check_unsigned_range<T>(res.value(), key, type_label);
}
} // namespace detail

[[nodiscard]] inline auto get_str(simdjson::ondemand::object& obj, std::string_view key, std::string_view def = {})
    -> std::expected<std::string, cfire::Error>
{
    auto f = obj[key];
    if (f.error() == simdjson::NO_SUCH_FIELD) return std::string(def);
    if (!f.error() && f.value().is_null()) return std::string(def);
    auto res = f.get_string();
    if (res.error()) {
        return cfire::make_error(std::format("missing or invalid string field '{}'", key));
    }
    return std::string(res.value());
}

[[nodiscard]] inline auto get_u8(simdjson::ondemand::object& obj, std::string_view key, std::uint8_t def = 0)
    -> std::expected<std::uint8_t, cfire::Error> { return detail::get_unsigned_field<std::uint8_t>(obj, key, def, "u8"); }

[[nodiscard]] inline auto get_u16(simdjson::ondemand::object& obj, std::string_view key, std::uint16_t def = 0)
    -> std::expected<std::uint16_t, cfire::Error> { return detail::get_unsigned_field<std::uint16_t>(obj, key, def, "u16"); }

[[nodiscard]] inline auto get_u32(simdjson::ondemand::object& obj, std::string_view key, std::uint32_t def = 0U)
    -> std::expected<std::uint32_t, cfire::Error> { return detail::get_unsigned_field<std::uint32_t>(obj, key, def, "u32"); }

[[nodiscard]] inline auto get_u64(simdjson::ondemand::object& obj, std::string_view key, std::uint64_t def = 0ULL)
    -> std::expected<std::uint64_t, cfire::Error> { return detail::get_unsigned_field<std::uint64_t>(obj, key, def, "u64"); }

[[nodiscard]] inline auto get_usize(simdjson::ondemand::object& obj, std::string_view key, std::size_t def = 0)
    -> std::expected<std::size_t, cfire::Error> { return detail::get_unsigned_field<std::size_t>(obj, key, def, "usize"); }

[[nodiscard]] inline auto get_i8(simdjson::ondemand::object& obj, std::string_view key, std::int8_t def = 0)
    -> std::expected<std::int8_t, cfire::Error> { return detail::get_signed_field<std::int8_t>(obj, key, def, "i8"); }

[[nodiscard]] inline auto get_i16(simdjson::ondemand::object& obj, std::string_view key, std::int16_t def = 0)
    -> std::expected<std::int16_t, cfire::Error> { return detail::get_signed_field<std::int16_t>(obj, key, def, "i16"); }

[[nodiscard]] inline auto get_i32(simdjson::ondemand::object& obj, std::string_view key, std::int32_t def = 0)
    -> std::expected<std::int32_t, cfire::Error> { return detail::get_signed_field<std::int32_t>(obj, key, def, "i32"); }

[[nodiscard]] inline auto get_i64(simdjson::ondemand::object& obj, std::string_view key, std::int64_t def = 0LL)
    -> std::expected<std::int64_t, cfire::Error> { return detail::get_signed_field<std::int64_t>(obj, key, def, "i64"); }

[[nodiscard]] inline auto get_isize(simdjson::ondemand::object& obj, std::string_view key, std::ptrdiff_t def = 0)
    -> std::expected<std::ptrdiff_t, cfire::Error> { return detail::get_signed_field<std::ptrdiff_t>(obj, key, def, "isize"); }

[[nodiscard]] inline auto get_f64(simdjson::ondemand::object& obj, std::string_view key, double def = 0.0)
    -> std::expected<double, cfire::Error>
{
    auto f = obj[key];
    if (f.error() == simdjson::NO_SUCH_FIELD) return def;
    if (!f.error() && f.value().is_null()) return def;
    auto res = f.get_double();
    if (res.error()) {
        return cfire::make_error(std::format("missing or invalid f64 field '{}'", key));
    }
    return res.value();
}

[[nodiscard]] inline auto get_f32(simdjson::ondemand::object& obj, std::string_view key, float def = 0.0f)
    -> std::expected<float, cfire::Error>
{
    auto res = get_f64(obj, key, static_cast<double>(def));
    if (!res) return std::unexpected(res.error());
    return static_cast<float>(*res);
}

[[nodiscard]] inline auto get_value(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<Value, cfire::Error>
{
    auto f = obj[key];
    if (f.error() == simdjson::NO_SUCH_FIELD) return Value{nullptr};
    if (f.error()) return cfire::make_error(std::format("missing or invalid json value field '{}'", key));
    simdjson::ondemand::value v;
    if (auto err = f.get(v); err) return cfire::make_error(std::format("invalid json value field '{}'", key));
    return detail::ondemand_to_dynamic(v);
}

template <typename Sub>
[[nodiscard]] inline auto get_obj(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<Sub, cfire::Error>
{
    auto f = obj[key];
    if (f.error() == simdjson::NO_SUCH_FIELD) return Sub{};
    if (!f.error() && f.value().is_null()) return Sub{};
    if constexpr (cfire::reflect_enum<Sub>::is_enum) {
        auto s = f.get_string();
        if (s.error()) {
            return cfire::make_error(std::format("missing or invalid string for enum field '{}'", key));
        }
        auto parsed = Sub::from_string(s.value());
        if (!parsed) {
            return cfire::make_error(std::format("invalid enum variant '{}' for field '{}'", std::string_view(s.value()), key));
        }
        return *parsed;
    } else {
        auto sub = f.get_object();
        if (sub.error()) {
            return cfire::make_error(std::format("missing or invalid object field '{}'", key));
        }
        return parse_struct<Sub>(sub.value());
    }
}

[[nodiscard]] inline auto get_bool(simdjson::ondemand::object& obj, std::string_view key, bool def = false)
    -> std::expected<bool, cfire::Error>
{
    auto f = obj[key];
    if (f.error() == simdjson::NO_SUCH_FIELD) return def;
    if (!f.error() && f.value().is_null()) return def;
    auto res = f.get_bool();
    if (res.error()) {
        return cfire::make_error(std::format("missing or invalid bool field '{}'", key));
    }
    return res.value();
}

template <typename Elem>
[[nodiscard]] inline auto get_vec(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<std::vector<Elem>, cfire::Error>
{
    auto f = obj[key];
    if (f.error() == simdjson::NO_SUCH_FIELD) return std::vector<Elem>{};
    if (!f.error() && f.value().is_null()) return std::vector<Elem>{};
    auto arr = f.get_array();
    if (arr.error()) {
        return cfire::make_error(std::format("missing or invalid array field '{}'", key));
    }
    std::vector<Elem> out;
    for (auto elem : arr.value()) {
        auto parsed = detail::parse_ondemand_elem<Elem>(elem, key);
        if (!parsed) return std::unexpected(parsed.error());
        out.push_back(std::move(*parsed));
    }
    return out;
}

template <typename T>
[[nodiscard]] inline auto get_opt(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<std::optional<T>, cfire::Error>
{
    auto f = obj[key];
    if (f.error() == simdjson::NO_SUCH_FIELD) return std::optional<T>{std::nullopt};
    if (f.error()) return cfire::make_error(std::format("invalid optional field '{}'", key));
    if (f.value().is_null()) return std::optional<T>{std::nullopt};
    auto parsed = detail::parse_ondemand_elem<T>(f, key);
    if (!parsed) return std::unexpected(parsed.error());
    return std::optional<T>{std::move(*parsed)};
}
#else
namespace detail {
template <typename T>
[[nodiscard]] inline auto get_signed_field_dyn(
    simdjson::ondemand::object& obj,
    std::string_view key,
    T def,
    std::string_view type_label
) -> std::expected<T, cfire::Error> {
    if (!obj.val) return def;
    auto v = obj.val->get(key);
    if (!v || v->is_null()) return def;
    if (!v->is_number()) return cfire::make_error(std::format("missing or invalid {} field '{}'", type_label, key));
    return check_signed_range<T>(v->as_int(), key, type_label);
}

template <typename T>
[[nodiscard]] inline auto get_unsigned_field_dyn(
    simdjson::ondemand::object& obj,
    std::string_view key,
    T def,
    std::string_view type_label
) -> std::expected<T, cfire::Error> {
    if (!obj.val) return def;
    auto v = obj.val->get(key);
    if (!v || v->is_null()) return def;
    if (!v->is_number() || v->as_int() < 0) {
        return cfire::make_error(std::format("missing or invalid {} field '{}'", type_label, key));
    }
    return check_unsigned_range<T>(static_cast<std::uint64_t>(v->as_int()), key, type_label);
}
} // namespace detail

[[nodiscard]] inline auto get_str(simdjson::ondemand::object& obj, std::string_view key, std::string_view def = {})
    -> std::expected<std::string, cfire::Error> {
    if (!obj.val) return std::string(def);
    auto v = obj.val->get(key);
    if (!v || v->is_null()) return std::string(def);
    if (!v->is_string()) return cfire::make_error(std::format("missing or invalid string field '{}'", key));
    return std::string(v->as_str());
}
[[nodiscard]] inline auto get_u8(simdjson::ondemand::object& obj, std::string_view key, std::uint8_t def = 0)
    -> std::expected<std::uint8_t, cfire::Error> { return detail::get_unsigned_field_dyn<std::uint8_t>(obj, key, def, "u8"); }
[[nodiscard]] inline auto get_u16(simdjson::ondemand::object& obj, std::string_view key, std::uint16_t def = 0)
    -> std::expected<std::uint16_t, cfire::Error> { return detail::get_unsigned_field_dyn<std::uint16_t>(obj, key, def, "u16"); }
[[nodiscard]] inline auto get_u32(simdjson::ondemand::object& obj, std::string_view key, std::uint32_t def = 0U)
    -> std::expected<std::uint32_t, cfire::Error> { return detail::get_unsigned_field_dyn<std::uint32_t>(obj, key, def, "u32"); }
[[nodiscard]] inline auto get_u64(simdjson::ondemand::object& obj, std::string_view key, std::uint64_t def = 0ULL)
    -> std::expected<std::uint64_t, cfire::Error> { return detail::get_unsigned_field_dyn<std::uint64_t>(obj, key, def, "u64"); }
[[nodiscard]] inline auto get_usize(simdjson::ondemand::object& obj, std::string_view key, std::size_t def = 0)
    -> std::expected<std::size_t, cfire::Error> { return detail::get_unsigned_field_dyn<std::size_t>(obj, key, def, "usize"); }
[[nodiscard]] inline auto get_i8(simdjson::ondemand::object& obj, std::string_view key, std::int8_t def = 0)
    -> std::expected<std::int8_t, cfire::Error> { return detail::get_signed_field_dyn<std::int8_t>(obj, key, def, "i8"); }
[[nodiscard]] inline auto get_i16(simdjson::ondemand::object& obj, std::string_view key, std::int16_t def = 0)
    -> std::expected<std::int16_t, cfire::Error> { return detail::get_signed_field_dyn<std::int16_t>(obj, key, def, "i16"); }
[[nodiscard]] inline auto get_i32(simdjson::ondemand::object& obj, std::string_view key, std::int32_t def = 0)
    -> std::expected<std::int32_t, cfire::Error> { return detail::get_signed_field_dyn<std::int32_t>(obj, key, def, "i32"); }
[[nodiscard]] inline auto get_i64(simdjson::ondemand::object& obj, std::string_view key, std::int64_t def = 0LL)
    -> std::expected<std::int64_t, cfire::Error> { return detail::get_signed_field_dyn<std::int64_t>(obj, key, def, "i64"); }
[[nodiscard]] inline auto get_isize(simdjson::ondemand::object& obj, std::string_view key, std::ptrdiff_t def = 0)
    -> std::expected<std::ptrdiff_t, cfire::Error> { return detail::get_signed_field_dyn<std::ptrdiff_t>(obj, key, def, "isize"); }
[[nodiscard]] inline auto get_f64(simdjson::ondemand::object& obj, std::string_view key, double def = 0.0)
    -> std::expected<double, cfire::Error> {
    if (!obj.val) return def;
    auto v = obj.val->get(key);
    if (!v || v->is_null()) return def;
    if (!v->is_number()) return cfire::make_error(std::format("missing or invalid f64 field '{}'", key));
    return v->as_float();
}
[[nodiscard]] inline auto get_f32(simdjson::ondemand::object& obj, std::string_view key, float def = 0.0f)
    -> std::expected<float, cfire::Error> {
    auto res = get_f64(obj, key, static_cast<double>(def));
    if (!res) return std::unexpected(res.error());
    return static_cast<float>(*res);
}
[[nodiscard]] inline auto get_value(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<Value, cfire::Error> {
    if (!obj.val) return Value{nullptr};
    auto v = obj.val->get(key);
    if (!v) return Value{nullptr};
    return *v;
}
template <typename Sub>
[[nodiscard]] inline auto get_obj(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<Sub, cfire::Error> {
    if (!obj.val) return Sub{};
    auto v = obj.val->get(key);
    if (!v || v->is_null()) return Sub{};
    return detail::parse_dynamic_value<Sub>(*v, key);
}
[[nodiscard]] inline auto get_bool(simdjson::ondemand::object& obj, std::string_view key, bool def = false)
    -> std::expected<bool, cfire::Error> {
    if (!obj.val) return def;
    auto v = obj.val->get(key);
    if (!v || v->is_null()) return def;
    if (!v->is_bool()) return cfire::make_error(std::format("missing or invalid bool field '{}'", key));
    return v->as_bool();
}
template <typename Elem>
[[nodiscard]] inline auto get_vec(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<std::vector<Elem>, cfire::Error> {
    if (!obj.val) return std::vector<Elem>{};
    auto v = obj.val->get(key);
    if (!v || v->is_null()) return std::vector<Elem>{};
    if (!v->is_array()) return cfire::make_error(std::format("missing or invalid array field '{}'", key));
    std::vector<Elem> out;
    for (const auto& elem : v->as_array()) {
        auto parsed = detail::parse_dynamic_value<Elem>(elem, key);
        if (!parsed) return std::unexpected(parsed.error());
        out.push_back(std::move(*parsed));
    }
    return out;
}

template <typename T>
[[nodiscard]] inline auto get_opt(simdjson::ondemand::object& obj, std::string_view key)
    -> std::expected<std::optional<T>, cfire::Error>
{
    if (!obj.val) return std::optional<T>{std::nullopt};
    auto v = obj.val->get(key);
    if (!v || v->is_null()) return std::optional<T>{std::nullopt};
    auto parsed = detail::parse_dynamic_value<T>(*v, key);
    if (!parsed) return std::unexpected(parsed.error());
    return std::optional<T>{std::move(*parsed)};
}
#endif

} // namespace cfire::json

