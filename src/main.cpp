// src/main.cpp — Native C++23 CLI Driver for cfire (.cf)
// Self-hosted via pure cfire (src/main.cf)

#include "main.hpp"

auto main(int argc, char** argv) -> int {
    std::vector<std::string> args;
    args.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    auto runtime_dir = cfire::main_driver::locate_runtime_dir().string();
    return selfhost_run_driver(args, runtime_dir);
}
