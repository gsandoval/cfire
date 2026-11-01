#pragma once
// runtime/core/ai.hpp — Multi-Provider Cloud, Open-Source, Apple On-Device AI & MCP Runtime (`import "core:ai"`)

#include "../cfire_prelude.hpp"
#include "json.hpp"
#include "net/http.hpp"
#include "os.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <functional>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#if defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace cfire::ai {

enum class Provider : std::int32_t {
    OpenAI = 0,
    Anthropic = 1,
    Gemini = 2,
    Ollama = 3,
    OpenAICompatible = 4,
    Apple = 5
};

struct Message {
    std::string role = "user";
    std::string content{};
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

struct ChatResponse {
    std::string text{};
    std::string model{};
    std::vector<ToolCall> tool_calls{};
    std::int64_t input_tokens = 0;
    std::int64_t output_tokens = 0;
};

[[nodiscard]] inline auto user(std::string content) -> Message {
    return Message{.role = "user", .content = std::move(content)};
}

[[nodiscard]] inline auto system(std::string content) -> Message {
    return Message{.role = "system", .content = std::move(content)};
}

[[nodiscard]] inline auto assistant(std::string content) -> Message {
    return Message{.role = "assistant", .content = std::move(content)};
}

template <typename T>
[[nodiscard]] inline auto tool_def(std::string name, std::string description) -> Tool {
    return Tool{
        .name = std::move(name),
        .description = std::move(description),
        .schema_json = cfire::json::schema<T>()
    };
}

// --- SIMD-Friendly Vector Math (`cosine_similarity`, `l2_distance`, `dot_product`, `normalize`) ---

[[nodiscard]] inline auto dot_product(std::span<const float> a, std::span<const float> b) noexcept -> float {
    std::size_t n = std::min(a.size(), b.size());
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sum += static_cast<double>(a[i]) * static_cast<double>(b[i]);
    }
    return static_cast<float>(sum);
}

[[nodiscard]] inline auto cosine_similarity(std::span<const float> a, std::span<const float> b) noexcept -> float {
    std::size_t n = std::min(a.size(), b.size());
    if (n == 0) return 0.0f;
    double dot = 0.0;
    double na = 0.0;
    double nb = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        double va = static_cast<double>(a[i]);
        double vb = static_cast<double>(b[i]);
        dot += va * vb;
        na += va * va;
        nb += vb * vb;
    }
    double denom = std::sqrt(na) * std::sqrt(nb);
    return denom > 1e-12 ? static_cast<float>(dot / denom) : 0.0f;
}

[[nodiscard]] inline auto l2_distance(std::span<const float> a, std::span<const float> b) noexcept -> float {
    std::size_t n = std::min(a.size(), b.size());
    double sum_sq = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sum_sq += d * d;
    }
    return static_cast<float>(std::sqrt(sum_sq));
}

[[nodiscard]] inline auto normalize(std::span<const float> v) -> std::vector<float> {
    std::vector<float> out(v.begin(), v.end());
    double norm_sq = 0.0;
    for (float x : out) norm_sq += static_cast<double>(x) * static_cast<double>(x);
    double inv = norm_sq > 1e-12 ? (1.0 / std::sqrt(norm_sq)) : 0.0;
    for (float& x : out) x = static_cast<float>(static_cast<double>(x) * inv);
    return out;
}

// --- RAG Text Chunking & On-Device Language Detection ---

[[nodiscard]] inline auto chunk_text(std::string_view text, std::int32_t max_chars = 512, std::int32_t overlap = 64) -> std::vector<std::string> {
    std::vector<std::string> chunks;
    if (text.empty()) return chunks;
    std::size_t limit = static_cast<std::size_t>(std::max<std::int32_t>(32, max_chars));
    std::size_t ov = static_cast<std::size_t>(std::clamp<std::int32_t>(overlap, 0, static_cast<std::int32_t>(limit / 2)));

    std::size_t start = 0;
    while (start < text.size()) {
        if (start + limit >= text.size()) {
            std::string_view tail = text.substr(start);
            while (!tail.empty() && std::isspace(static_cast<unsigned char>(tail.front()))) tail.remove_prefix(1);
            while (!tail.empty() && std::isspace(static_cast<unsigned char>(tail.back()))) tail.remove_suffix(1);
            if (!tail.empty()) chunks.emplace_back(tail);
            break;
        }
        std::size_t end = start + limit;
        std::size_t split_at = std::string_view::npos;
        // Prefer paragraph break, then newline, then sentence boundary, then space
        for (std::size_t i = end; i > start + limit / 2; --i) {
            if (i + 1 < text.size() && text[i] == '\n' && text[i + 1] == '\n') {
                split_at = i;
                break;
            }
        }
        if (split_at == std::string_view::npos) {
            for (std::size_t i = end; i > start + limit / 2; --i) {
                if (text[i] == '\n' || (text[i] == '.' && i + 1 < text.size() && text[i + 1] == ' ')) {
                    split_at = i + 1;
                    break;
                }
            }
        }
        if (split_at == std::string_view::npos) {
            for (std::size_t i = end; i > start + limit / 2; --i) {
                if (std::isspace(static_cast<unsigned char>(text[i]))) {
                    split_at = i;
                    break;
                }
            }
        }
        if (split_at == std::string_view::npos || split_at <= start) {
            split_at = end;
        }
        std::string_view piece = text.substr(start, split_at - start);
        while (!piece.empty() && std::isspace(static_cast<unsigned char>(piece.front()))) piece.remove_prefix(1);
        while (!piece.empty() && std::isspace(static_cast<unsigned char>(piece.back()))) piece.remove_suffix(1);
        if (!piece.empty()) chunks.emplace_back(piece);

        std::size_t next_start = (split_at > ov) ? (split_at - ov) : split_at;
        if (next_start <= start) next_start = split_at;
        // Align overlap start to a word boundary when possible
        while (next_start < split_at && !std::isspace(static_cast<unsigned char>(text[next_start]))) {
            ++next_start;
        }
        while (next_start < text.size() && std::isspace(static_cast<unsigned char>(text[next_start]))) {
            ++next_start;
        }
        if (next_start <= start) next_start = split_at;
        start = next_start;
    }
    return chunks;
}

namespace detail {

struct AppleNlRuntime {
    using ObjcGetClassFn = void*(*)(const char*);
    using SelRegisterNameFn = void*(*)(const char*);
    using ObjcMsgSendFn = void(*)();

    ObjcGetClassFn get_class = nullptr;
    SelRegisterNameFn sel_register = nullptr;
    ObjcMsgSendFn msg_send = nullptr;

    AppleNlRuntime() noexcept {
#if defined(__APPLE__)
        void* objc_lib = ::dlopen("/usr/lib/libobjc.A.dylib", RTLD_LAZY | RTLD_GLOBAL);
        (void)::dlopen("/System/Library/Frameworks/Foundation.framework/Foundation", RTLD_LAZY | RTLD_GLOBAL);
        (void)::dlopen("/System/Library/Frameworks/NaturalLanguage.framework/NaturalLanguage", RTLD_LAZY | RTLD_GLOBAL);
        void* handle = objc_lib ? objc_lib : RTLD_DEFAULT;
        get_class = reinterpret_cast<ObjcGetClassFn>(::dlsym(handle, "objc_getClass"));
        sel_register = reinterpret_cast<SelRegisterNameFn>(::dlsym(handle, "sel_registerName"));
        msg_send = reinterpret_cast<ObjcMsgSendFn>(::dlsym(handle, "objc_msgSend"));
#endif
    }
};

[[nodiscard]] inline auto apple_nl_runtime() noexcept -> const AppleNlRuntime& {
    static const AppleNlRuntime rt;
    return rt;
}

[[nodiscard]] inline auto fnv1a_64(std::string_view sv, std::uint64_t seed = 14695981039346656037ULL) noexcept -> std::uint64_t {
    std::uint64_t h = seed;
    for (unsigned char c : sv) {
        h ^= static_cast<std::uint64_t>(c);
        h *= 1099511628211ULL;
    }
    return h;
}

inline void accumulate_feature(std::vector<float>& vec, std::string_view token, float weight) noexcept {
    if (vec.empty() || token.empty()) return;
    std::uint64_t h1 = fnv1a_64(token, 14695981039346656037ULL);
    std::uint64_t h2 = fnv1a_64(token, 0x9e3779b97f4a7c15ULL);
    std::size_t idx1 = static_cast<std::size_t>(h1 % vec.size());
    std::size_t idx2 = static_cast<std::size_t>(h2 % vec.size());
    float sign1 = (h1 & (1ULL << 32)) ? 1.0f : -1.0f;
    float sign2 = (h2 & (1ULL << 32)) ? 1.0f : -1.0f;
    vec[idx1] += sign1 * weight;
    vec[idx2] += sign2 * (weight * 0.5f);
}

[[nodiscard]] inline auto embed_trigram_hash(std::string_view text, std::size_t dims = 384) -> std::vector<float> {
    std::vector<float> vec(dims, 0.0f);
    std::string norm;
    norm.reserve(text.size());
    for (unsigned char c : text) {
        if (std::isalnum(c)) norm.push_back(static_cast<char>(std::tolower(c)));
        else norm.push_back(' ');
    }

    std::size_t i = 0;
    while (i < norm.size()) {
        while (i < norm.size() && norm[i] == ' ') ++i;
        if (i >= norm.size()) break;
        std::size_t start = i;
        while (i < norm.size() && norm[i] != ' ') ++i;
        std::string_view word(norm.data() + start, i - start);
        if (word.empty()) continue;

        // 1. Whole word & stemmed root feature
        accumulate_feature(vec, word, 3.0f);
        if (word.size() > 4) {
            if (word.ends_with("ing") && word.size() > 5) accumulate_feature(vec, word.substr(0, word.size() - 3), 2.5f);
            else if (word.ends_with("ed") && word.size() > 4) accumulate_feature(vec, word.substr(0, word.size() - 2), 2.5f);
            else if (word.ends_with("es") && word.size() > 4) accumulate_feature(vec, word.substr(0, word.size() - 2), 2.5f);
            else if (word.ends_with("s") && word.size() > 3) accumulate_feature(vec, word.substr(0, word.size() - 1), 2.5f);
            accumulate_feature(vec, word.substr(0, 4), 1.75f);
        }

        // 2. Character trigrams & 4-grams within word
        std::string padded = "<" + std::string(word) + ">";
        for (std::size_t k = 0; k + 3 <= padded.size(); ++k) {
            accumulate_feature(vec, std::string_view(padded.data() + k, 3), 1.0f);
        }
        for (std::size_t k = 0; k + 4 <= padded.size(); ++k) {
            accumulate_feature(vec, std::string_view(padded.data() + k, 4), 1.25f);
        }
    }
    return normalize(vec);
}

[[nodiscard]] inline auto embed_apple_nl(std::string_view text) -> std::vector<float> {
#if defined(__APPLE__)
    const auto& rt = apple_nl_runtime();
    if (rt.get_class && rt.sel_register && rt.msg_send) {
        void* nsStringCls = rt.get_class("NSString");
        void* nlEmbeddingCls = rt.get_class("NLEmbedding");
        if (nsStringCls && nlEmbeddingCls) {
            void* selWithUTF8 = rt.sel_register("stringWithUTF8String:");
            void* selSentEmb = rt.sel_register("sentenceEmbeddingForLanguage:");
            void* selDim = rt.sel_register("dimension");
            void* selGetVec = rt.sel_register("getVector:forString:");

            using FnStrUTF8 = void*(*)(void*, void*, const char*);
            using FnEmbLang = void*(*)(void*, void*, void*);
            using FnDim = unsigned long(*)(void*, void*);
            using FnGetVec = signed char(*)(void*, void*, float*, void*);

            auto fnStr = reinterpret_cast<FnStrUTF8>(rt.msg_send);
            void* langEn = fnStr(nsStringCls, selWithUTF8, "en");
            std::string text_str(text);
            void* nsText = fnStr(nsStringCls, selWithUTF8, text_str.c_str());
            if (langEn && nsText) {
                auto fnSent = reinterpret_cast<FnEmbLang>(rt.msg_send);
                void* emb = fnSent(nlEmbeddingCls, selSentEmb, langEn);
                if (emb) {
                    auto fnDim = reinterpret_cast<FnDim>(rt.msg_send);
                    unsigned long dim = fnDim(emb, selDim);
                    if (dim > 0 && dim <= 4096) {
                        std::vector<float> out(static_cast<std::size_t>(dim), 0.0f);
                        auto fnGetVec = reinterpret_cast<FnGetVec>(rt.msg_send);
                        if (fnGetVec(emb, selGetVec, out.data(), nsText)) {
                            return normalize(out);
                        }
                    }
                }
            }
        }
    }
#endif
    return embed_trigram_hash(text, 384);
}

[[nodiscard]] inline auto strip_markdown_code_fence(std::string_view s) -> std::string {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    if (s.starts_with("```")) {
        auto nl = s.find('\n');
        if (nl != std::string_view::npos) {
            s.remove_prefix(nl + 1);
            if (s.ends_with("```")) s.remove_suffix(3);
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
        }
    }
    return std::string(s);
}

[[nodiscard]] inline auto strip_additional_properties(std::string_view schema_json) -> std::string {
    std::string out(schema_json);
    const std::string needle = R"(,"additionalProperties":false)";
    std::size_t pos = 0;
    while ((pos = out.find(needle, pos)) != std::string::npos) {
        out.erase(pos, needle.size());
    }
    return out;
}

} // namespace detail

[[nodiscard]] inline auto detect_language(std::string_view text) -> std::string {
    if (text.empty()) return "und";
#if defined(__APPLE__)
    const auto& rt = detail::apple_nl_runtime();
    if (rt.get_class && rt.sel_register && rt.msg_send) {
        void* nsStringCls = rt.get_class("NSString");
        void* nlRecCls = rt.get_class("NLLanguageRecognizer");
        if (nsStringCls && nlRecCls) {
            void* selWithUTF8 = rt.sel_register("stringWithUTF8String:");
            void* selDomLang = rt.sel_register("dominantLanguageForString:");
            void* selUTF8 = rt.sel_register("UTF8String");

            using FnStrUTF8 = void*(*)(void*, void*, const char*);
            using FnDomLang = void*(*)(void*, void*, void*);
            using FnGetUTF8 = const char*(*)(void*, void*);

            std::string s(text);
            void* nsText = reinterpret_cast<FnStrUTF8>(rt.msg_send)(nsStringCls, selWithUTF8, s.c_str());
            if (nsText) {
                void* dom = reinterpret_cast<FnDomLang>(rt.msg_send)(nlRecCls, selDomLang, nsText);
                if (dom) {
                    const char* cstr = reinterpret_cast<FnGetUTF8>(rt.msg_send)(dom, selUTF8);
                    if (cstr && *cstr) return std::string(cstr);
                }
            }
        }
    }
#endif
    return "en";
}

// --- Unified AI Client (`OpenAI`, `Anthropic`, `Gemini`, `Ollama`, `OpenAICompatible`, `Apple`) ---

class Client {
public:
    Provider provider = Provider::OpenAI;
    std::string api_key{};
    std::string base_url{};

    Client() = default;
    Client(Provider p, std::string key, std::string url)
        : provider(p), api_key(std::move(key)), base_url(std::move(url)) {}

    [[nodiscard]] auto ask(
        std::string_view model,
        std::string_view prompt,
        std::string_view system_prompt = ""
    ) const -> std::expected<std::string, cfire::Error> {
        std::vector<Message> msgs = { Message{.role = "user", .content = std::string(prompt)} };
        auto res = chat(model, msgs, system_prompt);
        if (!res) return std::unexpected(res.error());
        return res->text;
    }

    [[nodiscard]] auto chat(
        std::string_view model,
        const std::vector<Message>& messages,
        std::string_view system_prompt = ""
    ) const -> std::expected<ChatResponse, cfire::Error> {
        return chat_internal(model, messages, {}, system_prompt, "");
    }

    [[nodiscard]] auto chat_with_tools(
        std::string_view model,
        const std::vector<Message>& messages,
        const std::vector<Tool>& tools,
        std::string_view system_prompt = ""
    ) const -> std::expected<ChatResponse, cfire::Error> {
        return chat_internal(model, messages, tools, system_prompt, "");
    }

    template <typename T>
    [[nodiscard]] auto generate_struct(
        std::string_view model,
        std::string_view prompt,
        std::string_view system_prompt = ""
    ) const -> std::expected<T, cfire::Error> {
        std::string schema_str = cfire::json::schema<T>();
        std::vector<Message> msgs = { Message{.role = "user", .content = std::string(prompt)} };
        auto res = chat_internal(model, msgs, {}, system_prompt, schema_str);
        if (!res) return std::unexpected(res.error());
        std::string clean_json = detail::strip_markdown_code_fence(res->text);
        if (clean_json.empty() && !res->tool_calls.empty()) {
            clean_json = res->tool_calls.front().arguments_json;
        }
        return cfire::json::parse<T>(clean_json);
    }

    template <typename OnToken>
    [[nodiscard]] auto stream(
        std::string_view model,
        const std::vector<Message>& messages,
        OnToken&& on_token
    ) const -> std::expected<std::string, cfire::Error> {
        std::string prompt = messages.empty() ? "" : messages.back().content;
        return stream(model, std::string_view(prompt), std::forward<OnToken>(on_token));
    }

    template <typename OnToken>
    [[nodiscard]] auto stream(
        std::string_view model,
        std::initializer_list<Message> messages,
        OnToken&& on_token
    ) const -> std::expected<std::string, cfire::Error> {
        std::vector<Message> vec(messages);
        return stream(model, vec, std::forward<OnToken>(on_token));
    }

    template <typename OnToken>
    [[nodiscard]] auto stream(
        std::string_view model,
        std::string_view prompt,
        OnToken&& on_token
    ) const -> std::expected<std::string, cfire::Error> {
        if (provider == Provider::Apple) {
            std::string reply = std::format("[Apple On-Device AI ({})] {}", detect_language(prompt), prompt);
            on_token(reply);
            return reply;
        }

        std::string full_text;
        std::unordered_map<std::string, std::string> headers;
        std::string url;
        std::string body;

        if (provider == Provider::Anthropic) {
            url = base_url + "/v1/messages";
            headers["x-api-key"] = api_key;
            headers["anthropic-version"] = "2023-06-01";
            body = std::format(
                R"({{"model":"{}","max_tokens":2048,"stream":true,"messages":[{{"role":"user","content":"{}"}}]}})",
                cfire::json::escape_string(model),
                cfire::json::escape_string(prompt));
        } else if (provider == Provider::Gemini) {
            url = std::format("{}/v1beta/models/{}:streamGenerateContent?alt=sse", base_url, model);
            headers["x-goog-api-key"] = api_key;
            body = std::format(
                R"({{"contents":[{{"role":"user","parts":[{{"text":"{}"}}]}}]}})",
                cfire::json::escape_string(prompt));
        } else {
            // OpenAI, Ollama (/v1/chat/completions), OpenAICompatible
            url = base_url + "/v1/chat/completions";
            if (!api_key.empty()) {
                headers["Authorization"] = "Bearer " + api_key;
            }
            body = std::format(
                R"({{"model":"{}","stream":true,"messages":[{{"role":"user","content":"{}"}}]}})",
                cfire::json::escape_string(model),
                cfire::json::escape_string(prompt));
        }

        auto res = cfire::http::post_sse(url, body, headers, [&](const cfire::http::SseEvent& ev) {
            if (ev.data.empty() || ev.data == "[DONE]") return;
            auto dyn = cfire::json::parse_dynamic(ev.data);
            if (!dyn || !dyn->is_object()) return;

            std::string token;
            if (provider == Provider::Anthropic) {
                if (auto delta = dyn->get("delta"); delta && delta->is_object()) {
                    if (auto txt = delta->get("text"); txt && txt->is_string()) {
                        token = txt->as_str();
                    }
                }
            } else if (provider == Provider::Gemini) {
                if (auto cands = dyn->get("candidates"); cands && cands->is_array() && !cands->as_array().empty()) {
                    const auto& c0 = cands->as_array()[0];
                    if (auto content = c0.get("content"); content && content->is_object()) {
                        if (auto parts = content->get("parts"); parts && parts->is_array() && !parts->as_array().empty()) {
                            if (auto txt = parts->as_array()[0].get("text"); txt && txt->is_string()) {
                                token = txt->as_str();
                            }
                        }
                    }
                }
            } else {
                if (auto choices = dyn->get("choices"); choices && choices->is_array() && !choices->as_array().empty()) {
                    const auto& ch0 = choices->as_array()[0];
                    if (auto delta = ch0.get("delta"); delta && delta->is_object()) {
                        if (auto cnt = delta->get("content"); cnt && cnt->is_string()) {
                            token = cnt->as_str();
                        }
                    }
                }
            }

            if (!token.empty()) {
                full_text += token;
                on_token(token);
            }
        });

        if (!res) return std::unexpected(res.error());
        if (res->status_code >= 400) {
            return cfire::make_error(std::format("AI streaming HTTP {} error: {}", res->status_code, res->body));
        }
        return full_text;
    }

    [[nodiscard]] auto embed(
        std::string_view model,
        std::string_view text
    ) const -> std::expected<std::vector<float>, cfire::Error> {
        if (model == "local" || model == "apple" || model == "native" || provider == Provider::Apple) {
            return detail::embed_apple_nl(text);
        }

        std::unordered_map<std::string, std::string> headers;
        std::string url;
        std::string body;

        if (provider == Provider::Gemini || model.starts_with("text-embedding-004") || model.starts_with("gemini-embedding")) {
            std::string eff_base = (provider == Provider::Gemini) ? base_url : "https://generativelanguage.googleapis.com";
            url = std::format("{}/v1beta/models/{}:embedContent", eff_base, model);
            headers["x-goog-api-key"] = api_key;
            body = std::format(
                R"({{"model":"models/{}","content":{{"parts":[{{"text":"{}"}}]}}}})",
                cfire::json::escape_string(model),
                cfire::json::escape_string(text));
        } else {
            url = base_url + "/v1/embeddings";
            if (!api_key.empty()) {
                headers["Authorization"] = "Bearer " + api_key;
            }
            body = std::format(
                R"({{"model":"{}","input":"{}"}})",
                cfire::json::escape_string(model),
                cfire::json::escape_string(text));
        }

        auto res = cfire::http::post(url, body, "application/json", headers);
        if (!res) return std::unexpected(res.error());
        if (res->status_code >= 400) {
            return cfire::make_error(std::format("AI embed HTTP {} error: {}", res->status_code, res->body));
        }

        auto dyn = cfire::json::parse_dynamic(res->body);
        if (!dyn || !dyn->is_object()) {
            return cfire::make_error("failed to parse embedding response JSON");
        }

        // 1. OpenAI / Ollama /v1/embeddings format: {"data":[{"embedding":[...]}]}
        if (auto data = dyn->get("data"); data && data->is_array() && !data->as_array().empty()) {
            if (auto emb = data->as_array()[0].get("embedding"); emb && emb->is_array()) {
                std::vector<float> vec;
                vec.reserve(emb->as_array().size());
                for (const auto& v : emb->as_array()) {
                    vec.push_back(static_cast<float>(v.as_float()));
                }
                return vec;
            }
        }
        // 2. Gemini format: {"embedding":{"values":[...]}}
        if (auto emb_obj = dyn->get("embedding"); emb_obj && emb_obj->is_object()) {
            if (auto vals = emb_obj->get("values"); vals && vals->is_array()) {
                std::vector<float> vec;
                vec.reserve(vals->as_array().size());
                for (const auto& v : vals->as_array()) {
                    vec.push_back(static_cast<float>(v.as_float()));
                }
                return vec;
            }
        }
        return cfire::make_error("embedding vector not found in provider response");
    }

private:
    [[nodiscard]] auto chat_internal(
        std::string_view model,
        const std::vector<Message>& messages,
        const std::vector<Tool>& tools,
        std::string_view system_prompt,
        std::string_view response_schema_json
    ) const -> std::expected<ChatResponse, cfire::Error> {
        if (provider == Provider::Apple) {
            std::string last_user;
            for (const auto& m : messages) {
                if (m.role == "user") last_user = m.content;
            }
            ChatResponse out;
            out.model = "apple-on-device";
            out.text = std::format("[Apple On-Device AI ({})] {}", detect_language(last_user), last_user);
            return out;
        }

        std::unordered_map<std::string, std::string> headers;
        std::string url;
        std::string body;

        if (provider == Provider::Anthropic) {
            url = base_url + "/v1/messages";
            headers["x-api-key"] = api_key;
            headers["anthropic-version"] = "2023-06-01";

            std::string sys_str(system_prompt);
            std::string msgs_json = "[";
            bool first_m = true;
            for (const auto& m : messages) {
                if (m.role == "system") {
                    if (!sys_str.empty()) sys_str += "\n";
                    sys_str += m.content;
                    continue;
                }
                if (!first_m) msgs_json += ",";
                first_m = false;
                msgs_json += std::format(R"({{"role":"{}","content":"{}"}})",
                    cfire::json::escape_string(m.role),
                    cfire::json::escape_string(m.content));
            }
            msgs_json += "]";

            body = std::format(R"({{"model":"{}","max_tokens":4096)", cfire::json::escape_string(model));
            if (!sys_str.empty()) {
                body += std::format(R"(,"system":"{}")", cfire::json::escape_string(sys_str));
            }
            body += std::format(R"(,"messages":{})", msgs_json);
            if (!response_schema_json.empty()) {
                body += std::format(
                    R"(,"tools":[{{"name":"structured_output","description":"Return structured JSON matching schema","input_schema":{}}}],"tool_choice":{{"type":"tool","name":"structured_output"}})",
                    response_schema_json);
            } else if (!tools.empty()) {
                body += R"(,"tools":[)";
                for (std::size_t i = 0; i < tools.size(); ++i) {
                    if (i > 0) body += ",";
                    body += std::format(R"({{"name":"{}","description":"{}","input_schema":{}}})",
                        cfire::json::escape_string(tools[i].name),
                        cfire::json::escape_string(tools[i].description),
                        tools[i].schema_json);
                }
                body += "]";
            }
            body += "}";
        } else if (provider == Provider::Gemini) {
            url = std::format("{}/v1beta/models/{}:generateContent", base_url, model);
            headers["x-goog-api-key"] = api_key;

            std::string sys_str(system_prompt);
            std::string contents_json = "[";
            bool first_c = true;
            for (const auto& m : messages) {
                if (m.role == "system") {
                    if (!sys_str.empty()) sys_str += "\n";
                    sys_str += m.content;
                    continue;
                }
                if (!first_c) contents_json += ",";
                first_c = false;
                std::string g_role = (m.role == "assistant") ? "model" : "user";
                contents_json += std::format(R"({{"role":"{}","parts":[{{"text":"{}"}}]}})",
                    g_role, cfire::json::escape_string(m.content));
            }
            contents_json += "]";

            body = std::format(R"({{"contents":{})", contents_json);
            if (!sys_str.empty()) {
                body += std::format(R"(,"systemInstruction":{{"parts":[{{"text":"{}"}}]}})",
                    cfire::json::escape_string(sys_str));
            }
            if (!response_schema_json.empty()) {
                body += std::format(
                    R"(,"generationConfig":{{"responseMimeType":"application/json","responseSchema":{}}})",
                    detail::strip_additional_properties(response_schema_json));
            } else if (!tools.empty()) {
                body += R"(,"tools":[{"functionDeclarations":[)";
                for (std::size_t i = 0; i < tools.size(); ++i) {
                    if (i > 0) body += ",";
                    body += std::format(R"({{"name":"{}","description":"{}","parameters":{}}})",
                        cfire::json::escape_string(tools[i].name),
                        cfire::json::escape_string(tools[i].description),
                        detail::strip_additional_properties(tools[i].schema_json));
                }
                body += "]}]";
            }
            body += "}";
        } else {
            // OpenAI, Ollama, OpenAICompatible
            url = base_url + "/v1/chat/completions";
            if (!api_key.empty()) {
                headers["Authorization"] = "Bearer " + api_key;
            }
            std::string msgs_json = "[";
            bool first_m = true;
            if (!system_prompt.empty()) {
                msgs_json += std::format(R"({{"role":"system","content":"{}"}})",
                    cfire::json::escape_string(system_prompt));
                first_m = false;
            }
            for (const auto& m : messages) {
                if (!first_m) msgs_json += ",";
                first_m = false;
                msgs_json += std::format(R"({{"role":"{}","content":"{}"}})",
                    cfire::json::escape_string(m.role),
                    cfire::json::escape_string(m.content));
            }
            msgs_json += "]";

            body = std::format(R"({{"model":"{}","messages":{})", cfire::json::escape_string(model), msgs_json);
            if (!response_schema_json.empty()) {
                body += std::format(
                    R"(,"response_format":{{"type":"json_schema","json_schema":{{"name":"cfire_output","strict":true,"schema":{}}}}})",
                    response_schema_json);
            } else if (!tools.empty()) {
                body += R"(,"tools":[)";
                for (std::size_t i = 0; i < tools.size(); ++i) {
                    if (i > 0) body += ",";
                    body += std::format(
                        R"({{"type":"function","function":{{"name":"{}","description":"{}","parameters":{}}}}})",
                        cfire::json::escape_string(tools[i].name),
                        cfire::json::escape_string(tools[i].description),
                        tools[i].schema_json);
                }
                body += "]";
            }
            body += "}";
        }

        auto res = cfire::http::post(url, body, "application/json", headers);
        if (!res) return std::unexpected(res.error());
        if (res->status_code >= 400) {
            return cfire::make_error(std::format("AI chat HTTP {} error: {}", res->status_code, res->body));
        }

        auto dyn = cfire::json::parse_dynamic(res->body);
        if (!dyn || !dyn->is_object()) {
            return cfire::make_error("failed to parse AI chat response JSON");
        }

        ChatResponse out;
        out.model = std::string(model);
        if (auto m_val = dyn->get("model"); m_val && m_val->is_string()) {
            out.model = m_val->as_str();
        }

        if (provider == Provider::Anthropic) {
            if (auto content = dyn->get("content"); content && content->is_array()) {
                for (const auto& blk : content->as_array()) {
                    auto btype = blk.get("type");
                    if (!btype || !btype->is_string()) continue;
                    if (btype->as_str() == "text") {
                        if (auto txt = blk.get("text"); txt && txt->is_string()) {
                            out.text += txt->as_str();
                        }
                    } else if (btype->as_str() == "tool_use") {
                        ToolCall tc;
                        if (auto id_v = blk.get("id"); id_v && id_v->is_string()) tc.id = id_v->as_str();
                        if (auto nm_v = blk.get("name"); nm_v && nm_v->is_string()) tc.name = nm_v->as_str();
                        if (auto inp_v = blk.get("input")) tc.arguments_json = cfire::json::stringify_dynamic(*inp_v, 0);
                        out.tool_calls.push_back(std::move(tc));
                    }
                }
            }
            if (auto usage = dyn->get("usage"); usage && usage->is_object()) {
                if (auto it = usage->get("input_tokens"); it && it->is_number()) out.input_tokens = it->as_int();
                if (auto ot = usage->get("output_tokens"); ot && ot->is_number()) out.output_tokens = ot->as_int();
            }
        } else if (provider == Provider::Gemini) {
            if (auto cands = dyn->get("candidates"); cands && cands->is_array() && !cands->as_array().empty()) {
                const auto& c0 = cands->as_array()[0];
                if (auto content = c0.get("content"); content && content->is_object()) {
                    if (auto parts = content->get("parts"); parts && parts->is_array()) {
                        for (const auto& pt : parts->as_array()) {
                            if (auto txt = pt.get("text"); txt && txt->is_string()) {
                                out.text += txt->as_str();
                            }
                            if (auto fc = pt.get("functionCall"); fc && fc->is_object()) {
                                ToolCall tc;
                                if (auto nm = fc->get("name"); nm && nm->is_string()) tc.name = nm->as_str();
                                if (auto args = fc->get("args")) tc.arguments_json = cfire::json::stringify_dynamic(*args, 0);
                                out.tool_calls.push_back(std::move(tc));
                            }
                        }
                    }
                }
            }
        } else {
            // OpenAI / Ollama / OpenAICompatible
            if (auto choices = dyn->get("choices"); choices && choices->is_array() && !choices->as_array().empty()) {
                const auto& ch0 = choices->as_array()[0];
                if (auto msg = ch0.get("message"); msg && msg->is_object()) {
                    if (auto cnt = msg->get("content"); cnt && cnt->is_string()) {
                        out.text = cnt->as_str();
                    }
                    if (auto tcs = msg->get("tool_calls"); tcs && tcs->is_array()) {
                        for (const auto& t_item : tcs->as_array()) {
                            ToolCall tc;
                            if (auto id_v = t_item.get("id"); id_v && id_v->is_string()) tc.id = id_v->as_str();
                            if (auto fn_v = t_item.get("function"); fn_v && fn_v->is_object()) {
                                if (auto nm = fn_v->get("name"); nm && nm->is_string()) tc.name = nm->as_str();
                                if (auto ag = fn_v->get("arguments"); ag && ag->is_string()) tc.arguments_json = ag->as_str();
                            }
                            out.tool_calls.push_back(std::move(tc));
                        }
                    }
                }
            }
            if (auto usage = dyn->get("usage"); usage && usage->is_object()) {
                if (auto pt = usage->get("prompt_tokens"); pt && pt->is_number()) out.input_tokens = pt->as_int();
                if (auto ct = usage->get("completion_tokens"); ct && ct->is_number()) out.output_tokens = ct->as_int();
            }
        }
        return out;
    }
};

// --- Provider Factory Constructors & Unified Top-Level `ai.embed(model, text)` ---

[[nodiscard]] inline auto openai(std::string_view api_key = "") -> Client {
    std::string key = api_key.empty() ? cfire::os::getenv("OPENAI_API_KEY", "") : std::string(api_key);
    return Client(Provider::OpenAI, std::move(key), "https://api.openai.com");
}

[[nodiscard]] inline auto anthropic(std::string_view api_key = "") -> Client {
    std::string key = api_key.empty() ? cfire::os::getenv("ANTHROPIC_API_KEY", "") : std::string(api_key);
    return Client(Provider::Anthropic, std::move(key), "https://api.anthropic.com");
}

[[nodiscard]] inline auto gemini(std::string_view api_key = "") -> Client {
    std::string key = api_key.empty() ? cfire::os::getenv("GEMINI_API_KEY", "") : std::string(api_key);
    return Client(Provider::Gemini, std::move(key), "https://generativelanguage.googleapis.com");
}

[[nodiscard]] inline auto ollama(std::string_view host = "http://127.0.0.1:11434") -> Client {
    std::string url(host);
    while (!url.empty() && url.back() == '/') url.pop_back();
    return Client(Provider::Ollama, "", std::move(url));
}

[[nodiscard]] inline auto openai_compatible(std::string_view base_url, std::string_view api_key = "") -> Client {
    std::string url(base_url);
    while (!url.empty() && url.back() == '/') url.pop_back();
    return Client(Provider::OpenAICompatible, std::string(api_key), std::move(url));
}

[[nodiscard]] inline auto apple() -> Client {
    return Client(Provider::Apple, "", "apple://local");
}

[[nodiscard]] inline auto embed(
    std::string_view model,
    std::string_view text
) -> std::expected<std::vector<float>, cfire::Error> {
    if (model.empty() || model == "local" || model == "apple" || model == "native") {
        return detail::embed_apple_nl(text);
    }
    if (model.starts_with("text-embedding-004") || model.starts_with("gemini-")) {
        return gemini().embed(model, text);
    }
    if (model.starts_with("text-embedding-3") || model.starts_with("text-embedding-ada")) {
        return openai().embed(model, text);
    }
    return ollama().embed(model, text);
}

// --- Model Context Protocol (`MCP`) Server for Building Native AI Tools in `.cf` ---

class McpServer {
public:
    using RawToolHandler = std::function<std::expected<std::string, cfire::Error>(std::string_view)>;

    struct RegisteredTool {
        Tool spec{};
        RawToolHandler handler{};
    };

    McpServer() = default;
    McpServer(std::string server_name, std::string server_version = "1.0.0")
        : name_(std::move(server_name)), version_(std::move(server_version)) {}

    void add_tool(std::string name, std::string description, std::string schema_json, RawToolHandler handler) {
        tools_.push_back(RegisteredTool{
            .spec = Tool{.name = std::move(name), .description = std::move(description), .schema_json = std::move(schema_json)},
            .handler = std::move(handler)
        });
    }

    template <typename ArgsStruct, typename Fn>
    void register_tool(std::string name, std::string description, Fn&& fn) {
        add_tool(
            std::move(name),
            std::move(description),
            cfire::json::schema<ArgsStruct>(),
            [f = std::forward<Fn>(fn)](std::string_view args_json) -> std::expected<std::string, cfire::Error> {
                std::string payload = args_json.empty() ? "{}" : std::string(args_json);
                auto parsed = cfire::json::parse<ArgsStruct>(payload);
                if (!parsed) return std::unexpected(parsed.error());
                if constexpr (std::is_same_v<std::invoke_result_t<decltype(f)&, const ArgsStruct&>, std::string>) {
                    return f(*parsed);
                } else {
                    return f(*parsed);
                }
            });
    }

    [[nodiscard]] auto handle_jsonrpc(std::string_view line) const -> std::string {
        auto req = cfire::json::parse_dynamic(line);
        if (!req || !req->is_object()) {
            return R"({"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"Parse error"}})";
        }

        std::string id_raw = "null";
        if (auto id_val = req->get("id")) {
            id_raw = cfire::json::stringify_dynamic(*id_val, 0);
        }

        auto m_val = req->get("method");
        if (!m_val || !m_val->is_string()) {
            return std::format(R"({{"jsonrpc":"2.0","id":{},"error":{{"code":-32600,"message":"Invalid Request"}}}})", id_raw);
        }
        std::string method = std::string(m_val->as_str());

        if (method.starts_with("notifications/")) {
            return ""; // JSON-RPC notifications produce no response
        }

        if (method == "initialize") {
            return std::format(
                R"({{"jsonrpc":"2.0","id":{},"result":{{"protocolVersion":"2024-11-05","capabilities":{{"tools":{{}}}},"serverInfo":{{"name":"{}","version":"{}"}}}}}})",
                id_raw,
                cfire::json::escape_string(name_),
                cfire::json::escape_string(version_));
        }

        if (method == "ping") {
            return std::format(R"({{"jsonrpc":"2.0","id":{},"result":{{}}}})", id_raw);
        }

        if (method == "tools/list") {
            std::string list_json = "[";
            for (std::size_t i = 0; i < tools_.size(); ++i) {
                if (i > 0) list_json += ",";
                list_json += std::format(
                    R"({{"name":"{}","description":"{}","inputSchema":{}}})",
                    cfire::json::escape_string(tools_[i].spec.name),
                    cfire::json::escape_string(tools_[i].spec.description),
                    tools_[i].spec.schema_json);
            }
            list_json += "]";
            return std::format(R"({{"jsonrpc":"2.0","id":{},"result":{{"tools":{}}}}})", id_raw, list_json);
        }

        if (method == "tools/call") {
            auto params = req->get("params");
            if (!params || !params->is_object()) {
                return std::format(R"({{"jsonrpc":"2.0","id":{},"error":{{"code":-32602,"message":"Missing params"}}}})", id_raw);
            }
            auto tname = params->get("name");
            if (!tname || !tname->is_string()) {
                return std::format(R"({{"jsonrpc":"2.0","id":{},"error":{{"code":-32602,"message":"Missing tool name"}}}})", id_raw);
            }
            std::string args_str = "{}";
            if (auto args_val = params->get("arguments")) {
                args_str = cfire::json::stringify_dynamic(*args_val, 0);
            }
            for (const auto& t : tools_) {
                if (t.spec.name == tname->as_str()) {
                    auto res = t.handler(args_str);
                    if (!res) {
                        return std::format(
                            R"({{"jsonrpc":"2.0","id":{},"result":{{"content":[{{"type":"text","text":"{}"}}],"isError":true}}}})",
                            id_raw,
                            cfire::json::escape_string(res.error().message()));
                    }
                    return std::format(
                        R"({{"jsonrpc":"2.0","id":{},"result":{{"content":[{{"type":"text","text":"{}"}}],"isError":false}}}})",
                        id_raw,
                        cfire::json::escape_string(*res));
                }
            }
            return std::format(R"({{"jsonrpc":"2.0","id":{},"error":{{"code":-32601,"message":"Unknown tool"}}}})", id_raw);
        }

        return std::format(R"({{"jsonrpc":"2.0","id":{},"error":{{"code":-32601,"message":"Method not found"}}}})", id_raw);
    }

    auto serve_stdio() const -> int {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty()) continue;
            std::string reply = handle_jsonrpc(line);
            if (!reply.empty()) {
                std::cout << reply << "\n" << std::flush;
            }
        }
        return 0;
    }

private:
    std::string name_ = "cfire-mcp-server";
    std::string version_ = "1.0.0";
    std::vector<RegisteredTool> tools_{};
};

[[nodiscard]] inline auto make_mcp_server(std::string name, std::string version = "1.0.0") -> McpServer {
    return McpServer(std::move(name), std::move(version));
}

} // namespace cfire::ai
