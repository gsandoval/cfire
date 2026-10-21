#pragma once
// src/lsp.hpp — Umbrella header for Language Server Protocol (LSP 3.17) Server (`cfire lsp`) & IDE Setup (`cfire ide`)
// Sub-modules:
//   - src/lsp/json_rpc.hpp  (JSON-RPC 2.0 framing, structure-aware key extraction, type inference & layout helpers)
//   - src/lsp/server.hpp    (`cfire::lsp::Server` language server implementation)
//   - src/lsp/ide_setup.hpp (`cfire::lsp::configure_ide` editor configuration bootstrapper)

#include "lsp/json_rpc.hpp"
#include "lsp/server.hpp"
#include "lsp/ide_setup.hpp"
