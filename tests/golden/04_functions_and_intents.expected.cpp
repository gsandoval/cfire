#include "cfire_prelude.hpp"

[[nodiscard]] inline auto divide_mod(std::int64_t a, std::int64_t b) -> std::tuple<std::int64_t, std::int64_t>;
inline auto append_value(std::vector<std::int32_t>& list, std::int32_t val) -> void;
[[nodiscard]] inline auto consume_payload(std::string&& payload) -> std::size_t;

#line 3 "04_functions_and_intents.cf"
[[nodiscard]] inline auto divide_mod(std::int64_t a, std::int64_t b) -> std::tuple<std::int64_t, std::int64_t> {
    #line 4 "04_functions_and_intents.cf"
    return std::make_tuple(cfire::div(a, b), cfire::mod(a, b));
}

#line 7 "04_functions_and_intents.cf"
inline auto append_value(std::vector<std::int32_t>& list, std::int32_t val) -> void {
    #line 8 "04_functions_and_intents.cf"
    (void)(cfire::vec_push(list, val));
}

#line 11 "04_functions_and_intents.cf"
[[nodiscard]] inline auto consume_payload(std::string&& payload) -> std::size_t {
    #line 12 "04_functions_and_intents.cf"
    return std::move(payload).size();
}

#line 15 "04_functions_and_intents.cf"
auto main(int argc, char** argv) -> int {
    cfire::detail::set_process_args(argc, argv);
    #line 16 "04_functions_and_intents.cf"
    auto [q, r] = divide_mod(23, 5);
    #line 17 "04_functions_and_intents.cf"
    std::vector<std::int32_t> nums = {1, 2};
    #line 18 "04_functions_and_intents.cf"
    (void)(append_value(nums, 3));
    #line 19 "04_functions_and_intents.cf"
    std::string msg = "temporary buffer";
    #line 20 "04_functions_and_intents.cf"
    auto len = consume_payload(std::move(msg));
    #line 21 "04_functions_and_intents.cf"
    (void)(std::println("q: {}, r: {}, nums_len: {}, len: {}", q, r, nums.size(), len));
    return 0;
}
