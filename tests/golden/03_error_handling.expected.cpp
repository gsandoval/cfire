#include "cfire_prelude.hpp"

[[nodiscard]] inline auto parse_even(cfire::str_ref s) -> std::expected<std::int64_t, cfire::Error>;
[[nodiscard]] inline auto find_name(std::int32_t id) -> std::optional<std::string>;

#line 3 "03_error_handling.cf"
[[nodiscard]] inline auto parse_even(cfire::str_ref s) -> std::expected<std::int64_t, cfire::Error> {
    #line 4 "03_error_handling.cf"
    if (s == "fail") {
        #line 5 "03_error_handling.cf"
        return cfire::make_error("simulated parse failure");
    }
    #line 7 "03_error_handling.cf"
    std::int64_t val = 42;
    #line 8 "03_error_handling.cf"
    if (cfire::mod(val, 2) != 0) {
        #line 9 "03_error_handling.cf"
        return cfire::make_error("odd number rejected");
    }
    #line 11 "03_error_handling.cf"
    return val;
}

#line 14 "03_error_handling.cf"
[[nodiscard]] inline auto find_name(std::int32_t id) -> std::optional<std::string> {
    #line 15 "03_error_handling.cf"
    if (id == 1) {
        #line 16 "03_error_handling.cf"
        return "alice";
    }
    #line 18 "03_error_handling.cf"
    return cfire::none;
}

#line 21 "03_error_handling.cf"
auto main(int argc, char** argv) -> int {
    cfire::detail::set_process_args(argc, argv);
    #line 22 "03_error_handling.cf"
    auto _try_1 = parse_even("42");
    if (!_try_1.has_value()) {
        std::println(stderr, "Error: {}", _try_1.error().message());
        return 1;
    }
    auto num = std::move(*_try_1);
    #line 23 "03_error_handling.cf"
    auto _res_2 = find_name(1);
    if (!_res_2.has_value()) {
        std::println(stderr, "Error: {}", cfire::make_error("user not found").error().message());
        return 1;
    }
    auto user = std::move(*_res_2);
    #line 24 "03_error_handling.cf"
    (void)(std::println("user {} with num {}", user, num));
    return 0;
}
