#include "cfire_prelude.hpp"
#include "core/sync.hpp"

[[nodiscard]] inline auto run_tasks() -> std::int32_t;
[[nodiscard]] inline auto apply_op(std::int32_t val, const std::function<std::int32_t(std::int32_t)>& op) -> std::int32_t;

#line 5 "05_concurrency_and_closures.cf"
[[nodiscard]] inline auto run_tasks() -> std::int32_t {
    #line 6 "05_concurrency_and_closures.cf"
    auto counter = 0;
    #line 7 "05_concurrency_and_closures.cf"
    {
        cfire::async::Scope scope(2);
        #line 8 "05_concurrency_and_closures.cf"
        (void)(scope.spawn([=]() mutable {
            #line 9 "05_concurrency_and_closures.cf"
            (void)(std::println("worker 1 finished"));
        }));
        #line 11 "05_concurrency_and_closures.cf"
        (void)(scope.spawn([=]() mutable {
            #line 12 "05_concurrency_and_closures.cf"
            (void)(std::println("worker 2 finished"));
        }));
        scope.join_all();
    }
    #line 15 "05_concurrency_and_closures.cf"
    return counter;
}

#line 18 "05_concurrency_and_closures.cf"
[[nodiscard]] inline auto apply_op(std::int32_t val, const std::function<std::int32_t(std::int32_t)>& op) -> std::int32_t {
    #line 19 "05_concurrency_and_closures.cf"
    return op(val);
}

#line 22 "05_concurrency_and_closures.cf"
auto main(int argc, char** argv) -> int {
    cfire::detail::set_process_args(argc, argv);
    #line 23 "05_concurrency_and_closures.cf"
    auto base = 10;
    #line 24 "05_concurrency_and_closures.cf"
    auto cl = [&](std::int32_t x) -> std::int32_t {
        return (x + base);
    };
    #line 25 "05_concurrency_and_closures.cf"
    auto res = apply_op(5, cl);
    #line 26 "05_concurrency_and_closures.cf"
    (void)run_tasks();
    #line 27 "05_concurrency_and_closures.cf"
    (void)(std::println("closure result: {}", res));
    return 0;
}
