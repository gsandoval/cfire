#include "cfire_prelude.hpp"

[[nodiscard]] inline auto test_branches(std::int32_t x) -> std::string;
[[nodiscard]] inline auto test_loops() -> std::int64_t;

#line 3 "control_flow.cf"
[[nodiscard]] inline auto test_branches(std::int32_t x) -> std::string {
    #line 4 "control_flow.cf"
    if (x > 10) {
        #line 5 "control_flow.cf"
        return "greater";
    } else {
        #line 6 "control_flow.cf"
        if (x == 10) {
            #line 7 "control_flow.cf"
            return "equal";
        } else {
            #line 9 "control_flow.cf"
            return "less";
        }
    }
}

#line 13 "control_flow.cf"
[[nodiscard]] inline auto test_loops() -> std::int64_t {
    #line 14 "control_flow.cf"
    std::int64_t sum = 0;
    #line 15 "control_flow.cf"
    for (std::int64_t i = static_cast<std::int64_t>(0), _range_end_i = static_cast<std::int64_t>(5); i < _range_end_i; ++i) {
        #line 16 "control_flow.cf"
        sum += i;
    }
    #line 18 "control_flow.cf"
    std::int64_t j = 0;
    #line 19 "control_flow.cf"
    while (j < 3) {
        #line 20 "control_flow.cf"
        j += 1;
    }
    #line 22 "control_flow.cf"
    return (sum + j);
}

#line 25 "control_flow.cf"
auto main(int argc, char** argv) -> int {
    cfire::detail::set_process_args(argc, argv);
    #line 26 "control_flow.cf"
    auto _defer_1 = cfire::scope_exit([&] { (void)(std::println("cleanup completed")); });
    #line 27 "control_flow.cf"
    std::string val = test_branches(15);
    #line 28 "control_flow.cf"
    std::int64_t total = test_loops();
    #line 29 "control_flow.cf"
    (void)(std::println("val: {}, total: {}", val, total));
    return 0;
}
