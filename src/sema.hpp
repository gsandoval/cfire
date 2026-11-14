#pragma once
// src/sema.hpp — C++23 Semantic Analyzer Umbrella Header

#include "sema/types.hpp"
#include "sema/stdlib_index.hpp"
#if !defined(CFIRE_SPLIT_TUS) || defined(CFIRE_TU_SEMA_INFER_IMPL) || \
    defined(CFIRE_TU_SEMA_EXPR_IMPL) || defined(CFIRE_TU_SEMA_CALL_IMPL) || \
    defined(CFIRE_TU_SEMA_STMT_IMPL)
#include "sema/fn_checker.hpp"
#include "sema/analyze.hpp"
#else
#include "sema/builtin_methods.hpp"
#include <expected>
#include <string>
#include <unordered_map>
#include <vector>

namespace cfire::frontend {

[[nodiscard]] auto analyze_resilient(
    FileAST ast,
    std::unordered_map<std::string, std::vector<std::string>> pre_cpp_fn_params = {}
) -> CheckedFile;

[[nodiscard]] auto analyze(
    FileAST ast,
    std::unordered_map<std::string, std::vector<std::string>> pre_cpp_fn_params = {}
) -> std::expected<CheckedFile, std::string>;

} // namespace cfire::frontend
#endif
