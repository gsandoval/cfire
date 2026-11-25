#include "cfire_prelude.hpp"
#if defined(_WIN32)
#include "detail/win32_clean_macros.hpp"
#ifdef interface
#undef interface
#endif
#endif

[[nodiscard]] inline auto parse_even(cfire::str_ref s) -> std::expected<std::int64_t, cfire::Error>;
[[nodiscard]] inline auto find_name(std::int32_t id) -> std::optional<std::string>;

#line 3 "error_handling.cf"
[[nodiscard]] inline auto parse_even(cfire::str_ref s) -> std::expected<std::int64_t, cfire::Error> {
    #line 4 "error_handling.cf"
    if (s == "fail") {
        #line 5 "error_handling.cf"
        return cfire::make_error("simulated parse failure");
    }
    #line 7 "error_handling.cf"
    std::int64_t val = 42;
    #line 8 "error_handling.cf"
    if (cfire::mod(val, 2) != 0) {
        #line 9 "error_handling.cf"
        return cfire::make_error("odd number rejected");
    }
    #line 11 "error_handling.cf"
    return val;
}

#line 14 "error_handling.cf"
[[nodiscard]] inline auto find_name(std::int32_t id) -> std::optional<std::string> {
    #line 15 "error_handling.cf"
    if (id == 1) {
        #line 16 "error_handling.cf"
        return "alice";
    }
    #line 18 "error_handling.cf"
    return cfire::none;
}

#line 21 "error_handling.cf"
auto main(int argc, char** argv) -> int {
    cfire::detail::set_process_args(argc, argv);
    #line 22 "error_handling.cf"
    std::expected<std::int64_t, cfire::Error> _try_1 = parse_even("42");
    if (!_try_1.has_value()) {
        std::println(stderr, "Error: {}", _try_1.error().message());
        return 1;
    }
    std::int64_t num = std::move(*_try_1);
    #line 23 "error_handling.cf"
    std::optional<std::string> _res_2 = find_name(1);
    if (!_res_2.has_value()) {
        std::println(stderr, "Error: {}", cfire::make_error("user not found").error().message());
        return 1;
    }
    std::string user = std::move(*_res_2);
    #line 24 "error_handling.cf"
    (void)(std::println("user {} with num {}", user, num));
    return 0;
}
