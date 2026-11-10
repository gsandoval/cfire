// @cfire-link: -lsimdjson
#define CFIRE_USE_SIMDJSON 1
#include "cfire_prelude.hpp"
#include "core/cli.hpp"
#include "core/json.hpp"

struct Shape;
struct Config;

#line 6 "02_structs_and_enums.cf"
struct Config {
    std::string Host{};
    std::uint16_t Port{};
};

template <>
struct cfire::reflect_struct<Config> {
    static constexpr std::string_view name = "Config";
    static constexpr std::size_t field_count = 2;
    static constexpr std::array<std::string_view, 2> field_names = {
        "Host",
        "Port",
    };
    static constexpr std::array<std::string_view, 2> field_tags = {
        R"tag(cli:"--host,-h" default:"127.0.0.1" json:"host")tag",
        R"tag(cli:"--port,-p" default:"8080"      json:"port")tag",
    };
};

template <>
struct cfire::testing_detail::Arbitrary<Config> {
    static auto generate(cfire::testing_detail::FuzzRng& rng, std::size_t iter) -> Config {
        Config out{};
        out.Host = cfire::testing_detail::Arbitrary<std::decay_t<decltype(out.Host)>>::generate(rng, iter + 0);
        out.Port = cfire::testing_detail::Arbitrary<std::decay_t<decltype(out.Port)>>::generate(rng, iter + 1);
        return out;
    }
};

template <>
inline auto cfire::cli::try_parse<Config>(int argc, char** argv) -> std::optional<Config> {
    Config out{};
    out.Host = "127.0.0.1";
    out.Port = static_cast<std::uint16_t>(8080);
    cfire::cli::FlagReader reader(argc, argv);
    reader.bind("--host", "-h", out.Host);
    reader.bind("--port", "-p", out.Port);
    if (reader.has_errors()) return std::nullopt;
    return out;
}

template <>
inline auto cfire::cli::parse<Config>(int argc, char** argv) -> Config {
    Config out{};
    out.Host = "127.0.0.1";
    out.Port = static_cast<std::uint16_t>(8080);
    cfire::cli::FlagReader reader(argc, argv);
    reader.bind("--host", "-h", out.Host);
    reader.bind("--port", "-p", out.Port);
    return out;
}

template <>
inline auto cfire::json::parse_struct<Config>(simdjson::ondemand::object obj) -> std::expected<Config, cfire::Error> {
    (void)obj;
    Config out{};
    out.Host = "127.0.0.1";
    CFIRE_TRY_ASSIGN(out.Host, cfire::json::get_str(obj, "host", out.Host));
    return out;
}

template <>
inline auto cfire::json::stringify_struct<Config>(const Config& val) -> std::string {
    (void)val;
    std::string out = "{";
    out += std::format("\"host\":\"{}\"", cfire::json::escape_string(val.Host));
    out += std::format(",\"port\":{}", val.Port);
    out += "}";
    return out;
}

template <>
inline auto cfire::json::schema_struct<Config>() -> std::string {
    std::string props = "{";
    std::string req = "[";
    props += std::format("\"host\":{}", cfire::json::schema<std::string>());
    req += "\"host\"";
    props += std::format(",\"port\":{}", cfire::json::schema<std::uint16_t>());
    req += ",\"port\"";
    props += "}";
    req += "]";
    return std::format(R"({{"type":"object","properties":{},"required":{},"additionalProperties":false}})", props, req);
}

#line 11 "02_structs_and_enums.cf"
struct Shape {
    struct _Circle {
        double _0{};
        bool operator==(const _Circle&) const = default;
    };
    struct _Rectangle {
        double _0{};
        double _1{};
        bool operator==(const _Rectangle&) const = default;
    };
    std::variant<_Circle, _Rectangle> _v;
    bool operator==(const Shape&) const = default;
    explicit operator std::int8_t() const noexcept { return static_cast<std::int8_t>(_v.index()); }
    explicit operator std::int16_t() const noexcept { return static_cast<std::int16_t>(_v.index()); }
    explicit operator std::int32_t() const noexcept { return static_cast<std::int32_t>(_v.index()); }
    explicit operator std::int64_t() const noexcept { return static_cast<std::int64_t>(_v.index()); }
    explicit operator std::uint8_t() const noexcept { return static_cast<std::uint8_t>(_v.index()); }
    explicit operator std::uint16_t() const noexcept { return static_cast<std::uint16_t>(_v.index()); }
    explicit operator std::uint32_t() const noexcept { return static_cast<std::uint32_t>(_v.index()); }
    explicit operator std::uint64_t() const noexcept { return static_cast<std::uint64_t>(_v.index()); }
    explicit operator std::size_t() const noexcept { return _v.index(); }
    explicit operator std::ptrdiff_t() const noexcept { return static_cast<std::ptrdiff_t>(_v.index()); }
    [[nodiscard]] auto to_i32() const noexcept -> std::int32_t { return static_cast<std::int32_t>(_v.index()); }
    [[nodiscard]] auto to_i64() const noexcept -> std::int64_t { return static_cast<std::int64_t>(_v.index()); }
    [[nodiscard]] auto to_string() const -> std::string_view {
        switch (_v.index()) {
        case 0: return "Circle";
        case 1: return "Rectangle";
        default: return "";
        }
    }
    [[nodiscard]] static auto Circle(double a0) -> Shape { return Shape{_Circle{std::move(a0)}}; }
    [[nodiscard]] static auto Rectangle(double a0, double a1) -> Shape { return Shape{_Rectangle{std::move(a0), std::move(a1)}}; }
    [[nodiscard]] static auto from_i32(std::int32_t val) -> std::optional<Shape> {
        switch (val) {
        default: return std::nullopt;
        }
    }
    [[nodiscard]] static auto from_i64(std::int64_t val) -> std::optional<Shape> {
        return from_i32(static_cast<std::int32_t>(val));
    }
    [[nodiscard]] static auto from_string(std::string_view s) -> std::optional<Shape> {
        return std::nullopt;
    }
};

template <>
struct std::formatter<Shape> : std::formatter<std::string_view> {
    auto format(const Shape& e, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(e.to_string(), ctx);
    }
};

template <>
struct cfire::reflect_enum<Shape> {
    static constexpr bool is_enum = true;
    static constexpr std::string_view name = "Shape";
    static constexpr std::size_t variant_count = 2;
    static constexpr std::array<std::string_view, 2> variant_names = {
        "Circle",
        "Rectangle",
    };
    static auto from_string(std::string_view s) -> std::optional<Shape> {
        return Shape::from_string(s);
    }
};

template <>
inline auto cfire::json::stringify_struct<Shape>(const Shape& val) -> std::string {
    return std::format("\"{}\"", val.to_string());
}

template <>
inline auto cfire::json::schema_struct<Shape>() -> std::string {
    return R"({"type":"string","enum":["Circle","Rectangle"]})";
}

[[nodiscard]] inline auto area(const Shape& s) -> double;

#line 16 "02_structs_and_enums.cf"
[[nodiscard]] inline auto area(const Shape& s) -> double {
    #line 17 "02_structs_and_enums.cf"
    double res = 0.0;
    #line 18 "02_structs_and_enums.cf"
    std::visit(cfire::overloaded{
        [&](const Shape::_Circle& _m) {
            const auto& r = _m._0;
            #line 20 "02_structs_and_enums.cf"
            res = ((3.1415926535 * r) * r);
        },
        [&](const Shape::_Rectangle& _m) {
            const auto& w = _m._0;
            const auto& h = _m._1;
            #line 23 "02_structs_and_enums.cf"
            res = (w * h);
        },
    }, s._v);
    #line 26 "02_structs_and_enums.cf"
    return res;
}

#line 29 "02_structs_and_enums.cf"
auto main(int argc, char** argv) -> int {
    cfire::detail::set_process_args(argc, argv);
    #line 30 "02_structs_and_enums.cf"
    auto c = Config{.Host = "localhost", .Port = 9000};
    #line 31 "02_structs_and_enums.cf"
    auto c_json = cfire::json::stringify(c);
    #line 32 "02_structs_and_enums.cf"
    auto s = Shape::Circle(2.5);
    #line 33 "02_structs_and_enums.cf"
    auto a = area(s);
    #line 34 "02_structs_and_enums.cf"
    (void)(std::println("config: {}, area: {:.2f}", c_json, a));
    return 0;
}
