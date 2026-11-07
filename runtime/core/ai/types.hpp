#pragma once
// runtime/core/ai/types.hpp — Core AI Message, Multimodal Attachment, Tool & Response Types

#include "../../cfire_prelude.hpp"
#include "../encoding.hpp"
#include "../fs.hpp"
#include "../json.hpp"
#include "../net/http.hpp"
#include "../os.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#if !defined(_WIN32)
#include <dlfcn.h>
#include <unistd.h>
#endif
#if defined(__AVX2__) || defined(__SSE2__)
#include <immintrin.h>
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#elif defined(__wasm_simd128__)
#include <wasm_simd128.h>
#endif

namespace cfire::ai {

enum class Provider : std::int32_t {
    OpenAI = 0,
    Anthropic = 1,
    Gemini = 2,
    Ollama = 3,
    OpenAICompatible = 4,
    Local = 5,
    Apple = 5,
    Mock = 6
};

struct Attachment {
    std::string filename{};
    std::string mime_type{};
    std::vector<std::byte> data{};
    std::string extracted_text{};
};

struct Tool {
    std::string name{};
    std::string description{};
    std::string schema_json = R"({"type":"object","properties":{},"additionalProperties":false})";
};

struct ToolCall {
    std::string id{};
    std::string name{};
    std::string arguments_json{};
};

struct Message {
    std::string role = "user"; // "system", "user", "assistant", "tool"
    std::string content{};
    std::vector<Attachment> attachments{};
    std::vector<ToolCall> tool_calls{};
    std::string tool_call_id{};
    std::string tool_name{};
    bool is_error = false;
};

struct DocumentSection {
    std::int32_t index = 1;
    std::string title{};
    std::string text{};
};

struct Document {
    std::string path{};
    std::string mime_type{};
    std::string format{};
    std::string text{};
    std::vector<DocumentSection> sections{};
};

struct ZipEntry {
    std::string name{};
    std::string content{};
};

struct ChatResponse {
    std::string text{};
    std::string model{};
    std::vector<ToolCall> tool_calls{};
    std::int64_t input_tokens = 0;
    std::int64_t output_tokens = 0;
};

using RawToolHandler = std::function<std::expected<std::string, cfire::Error>(std::string_view)>;

struct RegisteredTool {
    Tool spec{};
    RawToolHandler handler{};
};

namespace detail {

template <typename T>
struct is_std_expected : std::false_type {};

template <typename V, typename E>
struct is_std_expected<std::expected<V, E>> : std::true_type {};

[[nodiscard]] inline auto trim_ascii(std::string_view sv) noexcept -> std::string_view {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
        sv.remove_suffix(1);
    }
    return sv;
}

template <typename R>
[[nodiscard]] inline auto serialize_tool_return(R&& val) -> std::expected<std::string, cfire::Error> {
    using DecayR = std::decay_t<R>;
    if constexpr (std::is_same_v<DecayR, std::string>) {
        return std::forward<R>(val);
    } else if constexpr (std::is_convertible_v<DecayR, std::string_view>) {
        return std::string(std::string_view(val));
    } else if constexpr (is_std_expected<DecayR>::value) {
        if (!val) return std::unexpected(val.error());
        using ValT = std::decay_t<typename DecayR::value_type>;
        if constexpr (std::is_void_v<ValT>) {
            return std::string("ok");
        } else {
            return serialize_tool_return(*std::forward<R>(val));
        }
    } else {
        return cfire::json::stringify(val);
    }
}

} // namespace detail

[[nodiscard]] inline auto user(std::string content) -> Message {
    return Message{.role = "user", .content = std::move(content)};
}

[[nodiscard]] inline auto user_with_attachment(std::string content, Attachment att) -> Message {
    Message m{.role = "user", .content = std::move(content)};
    m.attachments.push_back(std::move(att));
    return m;
}

[[nodiscard]] inline auto system(std::string content) -> Message {
    return Message{.role = "system", .content = std::move(content)};
}

[[nodiscard]] inline auto assistant(std::string content) -> Message {
    return Message{.role = "assistant", .content = std::move(content)};
}

[[nodiscard]] inline auto assistant_tool_calls(std::vector<ToolCall> calls, std::string content = "") -> Message {
    return Message{
        .role = "assistant",
        .content = std::move(content),
        .tool_calls = std::move(calls)
    };
}

[[nodiscard]] inline auto tool_result(
    std::string tool_call_id,
    std::string tool_name,
    std::string content,
    bool is_error = false
) -> Message {
    return Message{
        .role = "tool",
        .content = std::move(content),
        .tool_call_id = std::move(tool_call_id),
        .tool_name = std::move(tool_name),
        .is_error = is_error
    };
}

template <typename T>
[[nodiscard]] inline auto tool_def(std::string name, std::string description) -> Tool {
    return Tool{
        .name = std::move(name),
        .description = std::move(description),
        .schema_json = cfire::json::schema<T>()
    };
}

} // namespace cfire::ai
