// src/tu_lsp.cpp — Translation Unit for `cfire lsp` (CF-198)

#define CFIRE_TU_LSP_SERVER_IMPL 1
#include "cli/dispatch.hpp"
#include "lsp.hpp"

#include <iostream>

namespace cfire::cli::tu {

auto run_lsp_tu(int argc, char** argv) -> int {
    (void)argc;
    (void)argv;
    cfire::lsp::Server srv;
    srv.handle_stream(std::cin, std::cout);
    return 0;
}

} // namespace cfire::cli::tu
