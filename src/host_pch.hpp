#pragma once
// src/host_pch.hpp — Precompiled header for parallel multi-TU host compiler build (CF-198)

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#if __has_include(<print>)
#include <print>
#endif
#include <ranges>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "../runtime/cfire_prelude.hpp"
#include "version.hpp"
#include "ast.hpp"
#include "parser.hpp"
#include "docgen/catalog_types.hpp"
#include "module_registry.hpp"
#include "diagnostics.hpp"
#include "sema/builtin_methods.hpp"
#include "sema/types.hpp"
