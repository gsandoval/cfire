// src/lsp/server_command.cpp — Translation Unit for `cfire lsp`

#define CFIRE_LSP_SERVER_IMPL 1
#include "../cli/dispatch.hpp"
#include "../lsp.hpp"

#include <iostream>

namespace cfire::cli {

auto run_lsp_command(int argc, char** argv) -> int {
    (void)argc;
    (void)argv;
    cfire::lsp::Server srv;
    srv.handle_stream(std::cin, std::cout);
    return 0;
}

} // namespace cfire::cli
