#pragma once
// runtime/core/ai.hpp — Multi-Provider Cloud, Open-Source, Apple On-Device AI & MCP Runtime (`import "core:ai"`)

#include "../cfire_prelude.hpp"
#include "encoding.hpp"
#include "fs.hpp"
#include "json.hpp"
#include "net/http.hpp"
#include "os.hpp"

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
#include <span>
#include <string>
#include <string_view>
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
    Apple = 5
};

struct Attachment {
    std::string filename{};
    std::string mime_type{};
    std::vector<std::byte> data{};
    std::string extracted_text{};
};

struct Message {
    std::string role = "user";
    std::string content{};
    std::vector<Attachment> attachments{};
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

template <typename T>
[[nodiscard]] inline auto tool_def(std::string name, std::string description) -> Tool {
    return Tool{
        .name = std::move(name),
        .description = std::move(description),
        .schema_json = cfire::json::schema<T>()
    };
}

// --- Hardware SIMD-Accelerated Vector Math (`cosine_similarity`, `l2_distance`, `dot_product`, `normalize`) ---

[[nodiscard]] inline auto dot_product(std::span<const float> a, std::span<const float> b) noexcept -> float {
    std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    double sum = 0.0;
#if defined(__AVX2__)
    __m256 v_sum = _mm256_setzero_ps();
    for (; i + 8 <= n; i += 8) {
        v_sum = _mm256_add_ps(v_sum, _mm256_mul_ps(_mm256_loadu_ps(a.data() + i), _mm256_loadu_ps(b.data() + i)));
    }
    alignas(32) float tmp[8];
    _mm256_store_ps(tmp, v_sum);
    for (int k = 0; k < 8; ++k) sum += tmp[k];
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    float32x4_t v_sum = vdupq_n_f32(0.0f);
    for (; i + 4 <= n; i += 4) {
        v_sum = vfmaq_f32(v_sum, vld1q_f32(a.data() + i), vld1q_f32(b.data() + i));
    }
    sum += static_cast<double>(vaddvq_f32(v_sum));
#elif defined(__wasm_simd128__)
    v128_t v_sum = wasm_f32x4_splat(0.0f);
    for (; i + 4 <= n; i += 4) {
        v_sum = wasm_f32x4_add(v_sum, wasm_f32x4_mul(wasm_v128_load(a.data() + i), wasm_v128_load(b.data() + i)));
    }
    alignas(16) float tmp[4];
    wasm_v128_store(tmp, v_sum);
    for (int k = 0; k < 4; ++k) sum += tmp[k];
#endif
    for (; i < n; ++i) {
        sum += static_cast<double>(a[i]) * static_cast<double>(b[i]);
    }
    return static_cast<float>(sum);
}

[[nodiscard]] inline auto cosine_similarity(std::span<const float> a, std::span<const float> b) noexcept -> float {
    std::size_t n = std::min(a.size(), b.size());
    if (n == 0) return 0.0f;
    std::size_t i = 0;
    double dot = 0.0;
    double na = 0.0;
    double nb = 0.0;
#if defined(__AVX2__)
    __m256 v_dot = _mm256_setzero_ps();
    __m256 v_na  = _mm256_setzero_ps();
    __m256 v_nb  = _mm256_setzero_ps();
    for (; i + 8 <= n; i += 8) {
        __m256 va = _mm256_loadu_ps(a.data() + i);
        __m256 vb = _mm256_loadu_ps(b.data() + i);
        v_dot = _mm256_add_ps(v_dot, _mm256_mul_ps(va, vb));
        v_na  = _mm256_add_ps(v_na,  _mm256_mul_ps(va, va));
        v_nb  = _mm256_add_ps(v_nb,  _mm256_mul_ps(vb, vb));
    }
    alignas(32) float t_dot[8], t_na[8], t_nb[8];
    _mm256_store_ps(t_dot, v_dot);
    _mm256_store_ps(t_na,  v_na);
    _mm256_store_ps(t_nb,  v_nb);
    for (int k = 0; k < 8; ++k) { dot += t_dot[k]; na += t_na[k]; nb += t_nb[k]; }
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    float32x4_t v_dot = vdupq_n_f32(0.0f);
    float32x4_t v_na  = vdupq_n_f32(0.0f);
    float32x4_t v_nb  = vdupq_n_f32(0.0f);
    for (; i + 4 <= n; i += 4) {
        float32x4_t va = vld1q_f32(a.data() + i);
        float32x4_t vb = vld1q_f32(b.data() + i);
        v_dot = vfmaq_f32(v_dot, va, vb);
        v_na  = vfmaq_f32(v_na,  va, va);
        v_nb  = vfmaq_f32(v_nb,  vb, vb);
    }
    dot += static_cast<double>(vaddvq_f32(v_dot));
    na  += static_cast<double>(vaddvq_f32(v_na));
    nb  += static_cast<double>(vaddvq_f32(v_nb));
#elif defined(__wasm_simd128__)
    v128_t v_dot = wasm_f32x4_splat(0.0f);
    v128_t v_na  = wasm_f32x4_splat(0.0f);
    v128_t v_nb  = wasm_f32x4_splat(0.0f);
    for (; i + 4 <= n; i += 4) {
        v128_t va = wasm_v128_load(a.data() + i);
        v128_t vb = wasm_v128_load(b.data() + i);
        v_dot = wasm_f32x4_add(v_dot, wasm_f32x4_mul(va, vb));
        v_na  = wasm_f32x4_add(v_na,  wasm_f32x4_mul(va, va));
        v_nb  = wasm_f32x4_add(v_nb,  wasm_f32x4_mul(vb, vb));
    }
    alignas(16) float t_dot[4], t_na[4], t_nb[4];
    wasm_v128_store(t_dot, v_dot);
    wasm_v128_store(t_na,  v_na);
    wasm_v128_store(t_nb,  v_nb);
    for (int k = 0; k < 4; ++k) { dot += t_dot[k]; na += t_na[k]; nb += t_nb[k]; }
#endif
    for (; i < n; ++i) {
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
    std::size_t i = 0;
    double sum_sq = 0.0;
#if defined(__AVX2__)
    __m256 v_sq = _mm256_setzero_ps();
    for (; i + 8 <= n; i += 8) {
        __m256 d = _mm256_sub_ps(_mm256_loadu_ps(a.data() + i), _mm256_loadu_ps(b.data() + i));
        v_sq = _mm256_add_ps(v_sq, _mm256_mul_ps(d, d));
    }
    alignas(32) float tmp[8];
    _mm256_store_ps(tmp, v_sq);
    for (int k = 0; k < 8; ++k) sum_sq += tmp[k];
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    float32x4_t v_sq = vdupq_n_f32(0.0f);
    for (; i + 4 <= n; i += 4) {
        float32x4_t d = vsubq_f32(vld1q_f32(a.data() + i), vld1q_f32(b.data() + i));
        v_sq = vfmaq_f32(v_sq, d, d);
    }
    sum_sq += static_cast<double>(vaddvq_f32(v_sq));
#elif defined(__wasm_simd128__)
    v128_t v_sq = wasm_f32x4_splat(0.0f);
    for (; i + 4 <= n; i += 4) {
        v128_t d = wasm_f32x4_sub(wasm_v128_load(a.data() + i), wasm_v128_load(b.data() + i));
        v_sq = wasm_f32x4_add(v_sq, wasm_f32x4_mul(d, d));
    }
    alignas(16) float tmp[4];
    wasm_v128_store(tmp, v_sq);
    for (int k = 0; k < 4; ++k) sum_sq += tmp[k];
#endif
    for (; i < n; ++i) {
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
        (void)::dlopen("/System/Library/Frameworks/PDFKit.framework/PDFKit", RTLD_LAZY | RTLD_GLOBAL);
        (void)::dlopen("/System/Library/Frameworks/Vision.framework/Vision", RTLD_LAZY | RTLD_GLOBAL);
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

[[nodiscard]] inline auto map_semantic_concept(std::string_view word) noexcept -> std::string_view {
    struct ConceptEntry { std::string_view term; std::string_view cluster; };
    static constexpr ConceptEntry table[] = {
        {"memory", "#c:mem"}, {"safety", "#c:mem"}, {"raii", "#c:mem"}, {"ownership", "#c:mem"},
        {"borrow", "#c:mem"}, {"leak", "#c:mem"}, {"leaks", "#c:mem"}, {"segfault", "#c:mem"},
        {"segfaults", "#c:mem"}, {"dangling", "#c:mem"}, {"lifetime", "#c:mem"}, {"alloc", "#c:mem"},
        {"allocation", "#c:mem"}, {"deterministic", "#c:mem"}, {"gc", "#c:mem"},
        {"vector", "#c:vec"}, {"vectors", "#c:vec"}, {"embedding", "#c:vec"}, {"embeddings", "#c:vec"},
        {"cosine", "#c:vec"}, {"similarity", "#c:vec"}, {"distance", "#c:vec"}, {"rag", "#c:vec"},
        {"retrieval", "#c:vec"}, {"semantic", "#c:vec"}, {"search", "#c:vec"}, {"chunk", "#c:vec"},
        {"sql", "#c:db"}, {"sqlite", "#c:db"}, {"database", "#c:db"}, {"table", "#c:db"},
        {"query", "#c:db"}, {"blob", "#c:db"}, {"wal", "#c:db"}, {"schema", "#c:db"},
        {"simd", "#c:perf"}, {"avx2", "#c:perf"}, {"neon", "#c:perf"}, {"vdsp", "#c:perf"},
        {"accelerate", "#c:perf"}, {"fast", "#c:perf"}, {"latency", "#c:perf"}, {"throughput", "#c:perf"},
        {"performance", "#c:perf"}, {"benchmark", "#c:perf"}, {"overhead", "#c:perf"},
        {"ai", "#c:ai"}, {"llm", "#c:ai"}, {"model", "#c:ai"}, {"prompt", "#c:ai"},
        {"chat", "#c:ai"}, {"token", "#c:ai"}, {"tokens", "#c:ai"}, {"stream", "#c:ai"},
        {"openai", "#c:ai"}, {"anthropic", "#c:ai"}, {"claude", "#c:ai"}, {"gemini", "#c:ai"},
        {"ollama", "#c:ai"}, {"mcp", "#c:ai"}, {"agent", "#c:ai"},
        {"pdf", "#c:doc"}, {"docx", "#c:doc"}, {"pptx", "#c:doc"}, {"xlsx", "#c:doc"},
        {"odt", "#c:doc"}, {"odp", "#c:doc"}, {"ods", "#c:doc"}, {"epub", "#c:doc"},
        {"slide", "#c:doc"}, {"slides", "#c:doc"}, {"presentation", "#c:doc"}, {"spreadsheet", "#c:doc"},
        {"document", "#c:doc"}, {"ocr", "#c:doc"}, {"vision", "#c:doc"}, {"image", "#c:doc"},
        {"audio", "#c:aud"}, {"sound", "#c:aud"}, {"wave", "#c:aud"}, {"wav", "#c:aud"},
        {"pcm", "#c:aud"}, {"synth", "#c:aud"}, {"tone", "#c:aud"}, {"frequency", "#c:aud"},
        {"mixer", "#c:aud"}, {"dsp", "#c:aud"}, {"glitch", "#c:aud"},
        {"http", "#c:net"}, {"https", "#c:net"}, {"tls", "#c:net"}, {"socket", "#c:net"},
        {"websocket", "#c:net"}, {"sse", "#c:net"}, {"server", "#c:net"}, {"client", "#c:net"},
        {"invoice", "#c:fin"}, {"receipt", "#c:fin"}, {"payment", "#c:fin"}, {"price", "#c:fin"},
        {"cost", "#c:fin"}, {"total", "#c:fin"}, {"revenue", "#c:fin"}, {"financial", "#c:fin"},
        {"quarterly", "#c:fin"}, {"budget", "#c:fin"}, {"vendor", "#c:fin"},
        {"bug", "#c:err"}, {"error", "#c:err"}, {"crash", "#c:err"}, {"exception", "#c:err"},
        {"failure", "#c:err"}, {"fault", "#c:err"}, {"defect", "#c:err"}, {"panic", "#c:err"}
    };
    for (const auto& e : table) {
        if (e.term == word) return e.cluster;
    }
    return {};
}

[[nodiscard]] inline auto is_stop_word(std::string_view w) noexcept -> bool {
    return w == "the" || w == "and" || w == "for" || w == "with" || w == "from" ||
           w == "that" || w == "this" || w == "are" || w == "was" || w == "into" ||
           w == "about" || w == "how" || w == "what" || w == "when" || w == "where";
}

[[nodiscard]] inline auto embed_trigram_hash(std::string_view text, std::size_t dims = 384) -> std::vector<float> {
    std::vector<float> vec(dims, 0.0f);
    std::string norm;
    norm.reserve(text.size());
    for (unsigned char c : text) {
        if (std::isalnum(c)) norm.push_back(static_cast<char>(std::tolower(c)));
        else norm.push_back(' ');
    }

    std::string prev_word;
    std::size_t i = 0;
    while (i < norm.size()) {
        while (i < norm.size() && norm[i] == ' ') ++i;
        if (i >= norm.size()) break;
        std::size_t start = i;
        while (i < norm.size() && norm[i] != ' ') ++i;
        std::string_view word(norm.data() + start, i - start);
        if (word.empty()) continue;

        if (is_stop_word(word)) {
            accumulate_feature(vec, word, 0.15f);
            continue;
        }

        // 1. Semantic concept cluster projection (bridges synonyms on offline Linux/WASM)
        if (auto cluster = map_semantic_concept(word); !cluster.empty()) {
            accumulate_feature(vec, cluster, 3.5f);
        }

        // 2. Whole word & stemmed root feature
        accumulate_feature(vec, word, 3.0f);
        std::string_view stem = word;
        if (word.size() > 4) {
            if (word.ends_with("ing") && word.size() > 5) stem = word.substr(0, word.size() - 3);
            else if (word.ends_with("ed") && word.size() > 4) stem = word.substr(0, word.size() - 2);
            else if (word.ends_with("es") && word.size() > 4) stem = word.substr(0, word.size() - 2);
            else if (word.ends_with("s") && word.size() > 3) stem = word.substr(0, word.size() - 1);
            if (stem != word) {
                accumulate_feature(vec, stem, 2.5f);
                if (auto sc = map_semantic_concept(stem); !sc.empty()) {
                    accumulate_feature(vec, sc, 3.0f);
                }
            }
            accumulate_feature(vec, word.substr(0, 4), 1.75f);
        }

        // 3. Word bigram feature
        if (!prev_word.empty()) {
            std::string bigram = prev_word + "_" + std::string(stem);
            accumulate_feature(vec, bigram, 1.5f);
        }
        prev_word.assign(stem.data(), stem.size());

        // 4. Character trigrams & 4-grams within word
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

// --- Zero-Dependency RFC 1951 (DEFLATE) / RFC 1950 (ZLIB) & ZIP Engine ---

struct BitStream {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t byte_pos = 0;
    std::uint32_t bit_buf = 0;
    int bit_count = 0;

    void fill(int need) noexcept {
        while (bit_count < need && byte_pos < size) {
            bit_buf |= static_cast<std::uint32_t>(data[byte_pos++]) << bit_count;
            bit_count += 8;
        }
    }

    [[nodiscard]] auto read_bits(int n) noexcept -> std::uint32_t {
        if (n == 0) return 0;
        fill(n);
        std::uint32_t val = bit_buf & ((1u << n) - 1u);
        bit_buf >>= n;
        bit_count -= n;
        return val;
    }

    void align_byte() noexcept {
        int drop = bit_count & 7;
        bit_buf >>= drop;
        bit_count -= drop;
    }
};

struct HuffTree {
    std::array<std::uint16_t, 16> counts{};
    std::array<std::uint16_t, 288> symbols{};

    void build(std::span<const std::uint8_t> lengths) noexcept {
        counts.fill(0);
        symbols.fill(0);
        for (std::uint8_t len : lengths) {
            if (len > 0 && len < 16) counts[len]++;
        }
        std::array<std::uint16_t, 16> offs{};
        std::uint16_t acc = 0;
        for (std::size_t i = 1; i < 16; ++i) {
            offs[i] = acc;
            acc += counts[i];
        }
        for (std::size_t sym = 0; sym < lengths.size(); ++sym) {
            std::uint8_t len = lengths[sym];
            if (len > 0 && len < 16) {
                symbols[offs[len]++] = static_cast<std::uint16_t>(sym);
            }
        }
    }

    [[nodiscard]] auto decode(BitStream& bs) const noexcept -> int {
        int code = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= static_cast<int>(bs.read_bits(1));
            int count = counts[static_cast<std::size_t>(len)];
            if (code - count < first) {
                return symbols[static_cast<std::size_t>(index + (code - first))];
            }
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }
};

[[nodiscard]] inline auto inflate_raw(std::span<const std::byte> src, std::size_t max_out = 64 * 1024 * 1024) -> std::vector<std::byte> {
    std::vector<std::byte> out;
    if (src.empty()) return out;
    BitStream bs{reinterpret_cast<const std::uint8_t*>(src.data()), src.size(), 0, 0, 0};

    static constexpr std::uint16_t len_base[29] = {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
    };
    static constexpr std::uint8_t len_extra[29] = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
    };
    static constexpr std::uint16_t dist_base[30] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
    };
    static constexpr std::uint8_t dist_extra[30] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };
    static constexpr std::uint8_t clen_order[19] = {
        16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
    };

    std::uint32_t bfinal = 0;
    while (!bfinal && (bs.byte_pos < bs.size || bs.bit_count > 0)) {
        bfinal = bs.read_bits(1);
        std::uint32_t btype = bs.read_bits(2);
        if (btype == 0) {
            bs.align_byte();
            std::uint32_t len = bs.read_bits(16);
            std::uint32_t nlen = bs.read_bits(16);
            (void)nlen;
            if (out.size() + len > max_out) break;
            for (std::uint32_t i = 0; i < len && bs.byte_pos < bs.size; ++i) {
                out.push_back(static_cast<std::byte>(bs.data[bs.byte_pos++]));
            }
        } else if (btype == 1 || btype == 2) {
            HuffTree lit_tree{};
            HuffTree dist_tree{};
            if (btype == 1) {
                std::array<std::uint8_t, 288> lit_lens{};
                for (std::size_t i = 0; i <= 143; ++i) lit_lens[i] = 8;
                for (std::size_t i = 144; i <= 255; ++i) lit_lens[i] = 9;
                for (std::size_t i = 256; i <= 279; ++i) lit_lens[i] = 7;
                for (std::size_t i = 280; i <= 287; ++i) lit_lens[i] = 8;
                std::array<std::uint8_t, 30> dist_lens{};
                dist_lens.fill(5);
                lit_tree.build(lit_lens);
                dist_tree.build(dist_lens);
            } else {
                std::uint32_t hlit = bs.read_bits(5) + 257;
                std::uint32_t hdist = bs.read_bits(5) + 1;
                std::uint32_t hclen = bs.read_bits(4) + 4;
                if (hlit > 286 || hdist > 30) break;
                std::array<std::uint8_t, 19> clen_lens{};
                for (std::uint32_t i = 0; i < hclen; ++i) {
                    clen_lens[clen_order[i]] = static_cast<std::uint8_t>(bs.read_bits(3));
                }
                HuffTree clen_tree{};
                clen_tree.build(clen_lens);
                std::vector<std::uint8_t> all_lens(hlit + hdist, 0);
                std::size_t idx = 0;
                while (idx < all_lens.size()) {
                    int sym = clen_tree.decode(bs);
                    if (sym < 0) break;
                    if (sym < 16) {
                        all_lens[idx++] = static_cast<std::uint8_t>(sym);
                    } else if (sym == 16) {
                        std::uint32_t rep = bs.read_bits(2) + 3;
                        std::uint8_t prev = idx > 0 ? all_lens[idx - 1] : 0;
                        while (rep-- > 0 && idx < all_lens.size()) all_lens[idx++] = prev;
                    } else if (sym == 17) {
                        std::uint32_t rep = bs.read_bits(3) + 3;
                        while (rep-- > 0 && idx < all_lens.size()) all_lens[idx++] = 0;
                    } else if (sym == 18) {
                        std::uint32_t rep = bs.read_bits(7) + 11;
                        while (rep-- > 0 && idx < all_lens.size()) all_lens[idx++] = 0;
                    }
                }
                lit_tree.build(std::span<const std::uint8_t>(all_lens.data(), hlit));
                dist_tree.build(std::span<const std::uint8_t>(all_lens.data() + hlit, hdist));
            }

            while (true) {
                int sym = lit_tree.decode(bs);
                if (sym < 0 || sym == 256) break;
                if (sym < 256) {
                    if (out.size() >= max_out) break;
                    out.push_back(static_cast<std::byte>(sym));
                } else {
                    int l_idx = sym - 257;
                    if (l_idx < 0 || l_idx >= 29) break;
                    std::size_t length = len_base[l_idx] + bs.read_bits(len_extra[l_idx]);
                    int d_idx = dist_tree.decode(bs);
                    if (d_idx < 0 || d_idx >= 30) break;
                    std::size_t dist = dist_base[d_idx] + bs.read_bits(dist_extra[d_idx]);
                    if (dist == 0 || dist > out.size() || out.size() + length > max_out) break;
                    for (std::size_t k = 0; k < length; ++k) {
                        out.push_back(out[out.size() - dist]);
                    }
                }
            }
        } else {
            break;
        }
    }
    return out;
}

[[nodiscard]] inline auto inflate_zlib_or_raw(std::span<const std::byte> src) -> std::vector<std::byte> {
    if (src.size() >= 2) {
        auto b0 = static_cast<std::uint8_t>(src[0]);
        auto b1 = static_cast<std::uint8_t>(src[1]);
        if ((b0 & 0x0F) == 8 && ((static_cast<std::uint32_t>(b0) * 256u + b1) % 31u == 0)) {
            std::size_t hdr = (b1 & 0x20) ? 6 : 2;
            if (src.size() > hdr) {
                auto dec = inflate_raw(src.subspan(hdr));
                if (!dec.empty()) return dec;
            }
        }
    }
    return inflate_raw(src);
}

[[nodiscard]] inline auto read_u16_le(const std::byte* p) noexcept -> std::uint16_t {
    return static_cast<std::uint16_t>(static_cast<std::uint8_t>(p[0])) |
           (static_cast<std::uint16_t>(static_cast<std::uint8_t>(p[1])) << 8);
}

[[nodiscard]] inline auto read_u32_le(const std::byte* p) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[0])) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[3])) << 24);
}

inline void append_u16_le(std::vector<std::byte>& buf, std::uint16_t v) {
    buf.push_back(static_cast<std::byte>(v & 0xFF));
    buf.push_back(static_cast<std::byte>((v >> 8) & 0xFF));
}

inline void append_u32_le(std::vector<std::byte>& buf, std::uint32_t v) {
    buf.push_back(static_cast<std::byte>(v & 0xFF));
    buf.push_back(static_cast<std::byte>((v >> 8) & 0xFF));
    buf.push_back(static_cast<std::byte>((v >> 16) & 0xFF));
    buf.push_back(static_cast<std::byte>((v >> 24) & 0xFF));
}

[[nodiscard]] inline auto crc32_bytes(std::string_view s) noexcept -> std::uint32_t {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char c : s) {
        crc ^= c;
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

[[nodiscard]] inline auto extract_zip_entries(std::span<const std::byte> zip_bytes) -> std::unordered_map<std::string, std::string> {
    std::unordered_map<std::string, std::string> files;
    std::size_t pos = 0;
    while (pos + 30 <= zip_bytes.size()) {
        std::uint32_t sig = read_u32_le(zip_bytes.data() + pos);
        if (sig != 0x04034b50u) {
            // Try advancing or stopping if Central Directory reached
            if (sig == 0x02014b50u || sig == 0x06054b50u) break;
            ++pos;
            continue;
        }
        std::uint16_t flags = read_u16_le(zip_bytes.data() + pos + 6);
        std::uint16_t method = read_u16_le(zip_bytes.data() + pos + 8);
        std::uint32_t comp_size = read_u32_le(zip_bytes.data() + pos + 18);
        std::uint16_t name_len = read_u16_le(zip_bytes.data() + pos + 26);
        std::uint16_t extra_len = read_u16_le(zip_bytes.data() + pos + 28);
        std::size_t data_offset = pos + 30 + name_len + extra_len;
        if (data_offset > zip_bytes.size()) break;

        std::string name(reinterpret_cast<const char*>(zip_bytes.data() + pos + 30), name_len);

        // If bit 3 (data descriptor) is set and comp_size == 0, scan Central Directory for actual size
        if ((flags & 0x08u) != 0 && comp_size == 0) {
            // Look up in central directory at the end of the file
            for (std::size_t cd = data_offset; cd + 46 <= zip_bytes.size(); ++cd) {
                if (read_u32_le(zip_bytes.data() + cd) == 0x02014b50u) {
                    std::uint32_t cd_comp = read_u32_le(zip_bytes.data() + cd + 20);
                    std::uint16_t cd_nlen = read_u16_le(zip_bytes.data() + cd + 28);
                    if (cd + 46 + cd_nlen <= zip_bytes.size()) {
                        std::string_view cd_name(reinterpret_cast<const char*>(zip_bytes.data() + cd + 46), cd_nlen);
                        if (cd_name == name) {
                            comp_size = cd_comp;
                            break;
                        }
                    }
                }
            }
        }

        if (data_offset + comp_size > zip_bytes.size()) break;
        std::span<const std::byte> payload = zip_bytes.subspan(data_offset, comp_size);
        if (method == 0) {
            files[name] = std::string(reinterpret_cast<const char*>(payload.data()), payload.size());
        } else if (method == 8) {
            auto dec = inflate_raw(payload);
            files[name] = std::string(reinterpret_cast<const char*>(dec.data()), dec.size());
        }
        pos = data_offset + comp_size;
    }
    return files;
}

[[nodiscard]] inline auto xml_unescape(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '&') {
            auto semi = s.find(';', i + 1);
            if (semi != std::string_view::npos && semi - i <= 10) {
                std::string_view ent = s.substr(i + 1, semi - i - 1);
                if (ent == "amp") { out.push_back('&'); i = semi; continue; }
                if (ent == "lt") { out.push_back('<'); i = semi; continue; }
                if (ent == "gt") { out.push_back('>'); i = semi; continue; }
                if (ent == "quot") { out.push_back('"'); i = semi; continue; }
                if (ent == "apos" || ent == "#39" || ent == "#x27") { out.push_back('\''); i = semi; continue; }
                if (ent == "nbsp" || ent == "#160") { out.push_back(' '); i = semi; continue; }
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

[[nodiscard]] inline auto trim_ws(std::string_view sv) -> std::string {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) sv.remove_suffix(1);
    return std::string(sv);
}

[[nodiscard]] inline auto extract_xml_text_runs(std::string_view xml, std::string_view tag_name) -> std::string {
    std::string out;
    std::string open_prefix = "<" + std::string(tag_name);
    std::string close_tag = "</" + std::string(tag_name) + ">";
    std::size_t pos = 0;
    while ((pos = xml.find(open_prefix, pos)) != std::string_view::npos) {
        std::size_t after = pos + open_prefix.size();
        if (after < xml.size() && xml[after] != '>' && !std::isspace(static_cast<unsigned char>(xml[after])) && xml[after] != '/') {
            pos = after;
            continue;
        }
        auto gt = xml.find('>', after);
        if (gt == std::string_view::npos) break;
        if (xml[gt - 1] == '/') {
            pos = gt + 1;
            continue;
        }
        auto end = xml.find(close_tag, gt + 1);
        if (end == std::string_view::npos) break;
        out += xml_unescape(xml.substr(gt + 1, end - (gt + 1)));
        pos = end + close_tag.size();
    }
    return out;
}

inline void parse_docx_xml(std::string_view xml, Document& doc) {
    doc.format = "docx";
    std::string full_md;
    std::string current_sec_title = "Document";
    std::string current_sec_body;
    std::int32_t sec_idx = 1;

    std::size_t pos = 0;
    while (pos < xml.size()) {
        auto next_p = xml.find("<w:p", pos);
        auto next_tbl = xml.find("<w:tbl", pos);
        if (next_p == std::string_view::npos && next_tbl == std::string_view::npos) break;

        if (next_tbl != std::string_view::npos && (next_p == std::string_view::npos || next_tbl < next_p)) {
            auto end_tbl = xml.find("</w:tbl>", next_tbl);
            if (end_tbl == std::string_view::npos) break;
            std::string_view tbl_xml = xml.substr(next_tbl, end_tbl - next_tbl);
            std::size_t tr_pos = 0;
            bool first_row = true;
            while ((tr_pos = tbl_xml.find("<w:tr", tr_pos)) != std::string_view::npos) {
                auto end_tr = tbl_xml.find("</w:tr>", tr_pos);
                if (end_tr == std::string_view::npos) break;
                std::string_view tr_xml = tbl_xml.substr(tr_pos, end_tr - tr_pos);
                std::vector<std::string> cells;
                std::size_t tc_pos = 0;
                while ((tc_pos = tr_xml.find("<w:tc", tc_pos)) != std::string_view::npos) {
                    auto end_tc = tr_xml.find("</w:tc>", tc_pos);
                    if (end_tc == std::string_view::npos) break;
                    cells.push_back(trim_ws(extract_xml_text_runs(tr_xml.substr(tc_pos, end_tc - tc_pos), "w:t")));
                    tc_pos = end_tc + 7;
                }
                if (!cells.empty()) {
                    std::string row_line = "| ";
                    for (const auto& c : cells) row_line += c + " | ";
                    row_line.pop_back();
                    full_md += row_line + "\n";
                    current_sec_body += row_line + "\n";
                    if (first_row) {
                        std::string sep_line = "|";
                        for (std::size_t k = 0; k < cells.size(); ++k) sep_line += " --- |";
                        full_md += sep_line + "\n";
                        current_sec_body += sep_line + "\n";
                        first_row = false;
                    }
                }
                tr_pos = end_tr + 7;
            }
            full_md += "\n";
            current_sec_body += "\n";
            pos = end_tbl + 8;
            continue;
        }

        // Check <w:p> or <w:p ...>
        std::size_t after_p = next_p + 4;
        if (after_p < xml.size() && xml[after_p] != '>' && !std::isspace(static_cast<unsigned char>(xml[after_p])) && xml[after_p] != '/') {
            pos = after_p;
            continue;
        }
        auto end_p = xml.find("</w:p>", next_p);
        if (end_p == std::string_view::npos) break;
        std::string_view p_xml = xml.substr(next_p, end_p - next_p);
        std::string text = trim_ws(extract_xml_text_runs(p_xml, "w:t"));
        if (!text.empty()) {
            int heading_level = 0;
            auto st_pos = p_xml.find("w:pStyle");
            if (st_pos != std::string_view::npos) {
                auto val_pos = p_xml.find("w:val=\"", st_pos);
                if (val_pos != std::string_view::npos) {
                    val_pos += 7;
                    auto q_end = p_xml.find('"', val_pos);
                    if (q_end != std::string_view::npos) {
                        std::string_view style_name = p_xml.substr(val_pos, q_end - val_pos);
                        if (style_name.starts_with("Heading") || style_name.starts_with("heading")) {
                            char last = style_name.back();
                            if (last >= '1' && last <= '6') heading_level = last - '0';
                            else heading_level = 1;
                        } else if (style_name == "Title") {
                            heading_level = 1;
                        } else if (style_name == "Subtitle") {
                            heading_level = 2;
                        }
                    }
                }
            }

            if (heading_level > 0) {
                if (!trim_ws(current_sec_body).empty()) {
                    doc.sections.push_back(DocumentSection{
                        .index = sec_idx++,
                        .title = current_sec_title,
                        .text = trim_ws(current_sec_body)
                    });
                    current_sec_body.clear();
                }
                current_sec_title = text;
                std::string hashes(static_cast<std::size_t>(heading_level), '#');
                std::string line = hashes + " " + text + "\n\n";
                full_md += line;
                current_sec_body += line;
            } else {
                full_md += text + "\n\n";
                current_sec_body += text + "\n\n";
            }
        }
        pos = end_p + 6;
    }

    if (!trim_ws(current_sec_body).empty()) {
        doc.sections.push_back(DocumentSection{
            .index = sec_idx,
            .title = current_sec_title,
            .text = trim_ws(current_sec_body)
        });
    }
    doc.text = trim_ws(full_md);
}

inline void parse_pptx_zip(const std::unordered_map<std::string, std::string>& entries, Document& doc) {
    doc.format = "pptx";
    struct SlideItem {
        int num = 0;
        std::string xml;
        std::string notes_xml;
    };
    std::vector<SlideItem> slides;
    for (const auto& [name, content] : entries) {
        if (name.starts_with("ppt/slides/slide") && name.ends_with(".xml")) {
            std::string_view num_sv(name);
            num_sv.remove_prefix(16); // "ppt/slides/slide"
            num_sv.remove_suffix(4);  // ".xml"
            int slide_num = 0;
            for (char c : num_sv) {
                if (std::isdigit(static_cast<unsigned char>(c))) slide_num = slide_num * 10 + (c - '0');
            }
            if (slide_num > 0) {
                std::string notes_key = std::format("ppt/notesSlides/notesSlide{}.xml", slide_num);
                std::string notes = entries.contains(notes_key) ? entries.at(notes_key) : "";
                slides.push_back(SlideItem{slide_num, content, std::move(notes)});
            }
        }
    }
    std::sort(slides.begin(), slides.end(), [](const SlideItem& a, const SlideItem& b) { return a.num < b.num; });

    std::string full_md;
    for (const auto& sl : slides) {
        std::string slide_title;
        std::vector<std::string> body_lines;

        // Parse <p:sp> shapes to distinguish title placeholder from body text
        std::size_t sp_pos = 0;
        while ((sp_pos = sl.xml.find("<p:sp", sp_pos)) != std::string::npos) {
            auto end_sp = sl.xml.find("</p:sp>", sp_pos);
            if (end_sp == std::string::npos) break;
            std::string_view sp_xml(sl.xml.data() + sp_pos, end_sp - sp_pos);
            bool is_title = (sp_xml.find("type=\"title\"") != std::string_view::npos) ||
                            (sp_xml.find("type=\"ctrTitle\"") != std::string_view::npos);

            std::size_t p_pos = 0;
            while ((p_pos = sp_xml.find("<a:p", p_pos)) != std::string_view::npos) {
                auto end_p = sp_xml.find("</a:p>", p_pos);
                if (end_p == std::string_view::npos) break;
                std::string para = trim_ws(extract_xml_text_runs(sp_xml.substr(p_pos, end_p - p_pos), "a:t"));
                if (!para.empty()) {
                    if (is_title && slide_title.empty()) slide_title = para;
                    else body_lines.push_back(para);
                }
                p_pos = end_p + 6;
            }
            sp_pos = end_sp + 7;
        }

        // Fallback if <a:t> wasn't inside <p:sp>
        if (slide_title.empty() && body_lines.empty()) {
            std::string raw = trim_ws(extract_xml_text_runs(sl.xml, "a:t"));
            if (!raw.empty()) body_lines.push_back(raw);
        }
        if (slide_title.empty()) {
            if (!body_lines.empty()) {
                slide_title = body_lines.front();
                body_lines.erase(body_lines.begin());
            } else {
                slide_title = std::format("Slide {}", sl.num);
            }
        }

        std::string sec_md = std::format("## Slide {}: {}\n", sl.num, slide_title);
        for (const auto& b : body_lines) {
            sec_md += "- " + b + "\n";
        }
        if (!sl.notes_xml.empty()) {
            std::string notes_txt = trim_ws(extract_xml_text_runs(sl.notes_xml, "a:t"));
            if (!notes_txt.empty()) {
                sec_md += "Notes: " + notes_txt + "\n";
            }
        }

        doc.sections.push_back(DocumentSection{
            .index = sl.num,
            .title = slide_title,
            .text = trim_ws(sec_md)
        });
        full_md += sec_md + "\n";
    }
    doc.text = trim_ws(full_md);
}

inline void parse_xlsx_zip(const std::unordered_map<std::string, std::string>& entries, Document& doc) {
    doc.format = "xlsx";
    std::vector<std::string> shared_strings;
    if (auto it = entries.find("xl/sharedStrings.xml"); it != entries.end()) {
        std::string_view ss_xml = it->second;
        std::size_t si_pos = 0;
        while ((si_pos = ss_xml.find("<si", si_pos)) != std::string_view::npos) {
            auto end_si = ss_xml.find("</si>", si_pos);
            if (end_si == std::string_view::npos) break;
            shared_strings.push_back(extract_xml_text_runs(ss_xml.substr(si_pos, end_si - si_pos), "t"));
            si_pos = end_si + 5;
        }
    }

    struct SheetItem {
        int num = 0;
        std::string xml;
    };
    std::vector<SheetItem> sheets;
    for (const auto& [name, content] : entries) {
        if (name.starts_with("xl/worksheets/sheet") && name.ends_with(".xml")) {
            std::string_view num_sv(name);
            num_sv.remove_prefix(19);
            num_sv.remove_suffix(4);
            int num = 0;
            for (char c : num_sv) {
                if (std::isdigit(static_cast<unsigned char>(c))) num = num * 10 + (c - '0');
            }
            if (num > 0) sheets.push_back(SheetItem{num, content});
        }
    }
    std::sort(sheets.begin(), sheets.end(), [](const SheetItem& a, const SheetItem& b) { return a.num < b.num; });

    std::string full_md;
    for (const auto& sh : sheets) {
        std::string sheet_title = std::format("Sheet {}", sh.num);
        std::string sheet_md = std::format("## {}\n", sheet_title);
        std::string_view xml = sh.xml;
        std::size_t row_pos = 0;
        bool first_row = true;
        while ((row_pos = xml.find("<row", row_pos)) != std::string_view::npos) {
            auto end_row = xml.find("</row>", row_pos);
            if (end_row == std::string_view::npos) break;
            std::string_view row_xml = xml.substr(row_pos, end_row - row_pos);
            std::vector<std::string> cells;
            std::size_t c_pos = 0;
            while ((c_pos = row_xml.find("<c", c_pos)) != std::string_view::npos) {
                if (c_pos + 2 < row_xml.size() && row_xml[c_pos + 2] != ' ' && row_xml[c_pos + 2] != '>') {
                    c_pos += 2;
                    continue;
                }
                auto gt = row_xml.find('>', c_pos);
                if (gt == std::string_view::npos) break;
                if (row_xml[gt - 1] == '/') {
                    cells.emplace_back("");
                    c_pos = gt + 1;
                    continue;
                }
                auto end_c = row_xml.find("</c>", gt);
                if (end_c == std::string_view::npos) break;
                std::string_view c_tag = row_xml.substr(c_pos, gt - c_pos);
                std::string_view c_body = row_xml.substr(gt + 1, end_c - (gt + 1));
                bool is_shared = c_tag.find("t=\"s\"") != std::string_view::npos;
                bool is_inline = c_tag.find("t=\"inlineStr\"") != std::string_view::npos;
                std::string val;
                if (is_inline) {
                    val = extract_xml_text_runs(c_body, "t");
                } else {
                    val = extract_xml_text_runs(c_body, "v");
                    if (is_shared && !val.empty()) {
                        std::size_t idx = static_cast<std::size_t>(std::strtoul(val.c_str(), nullptr, 10));
                        if (idx < shared_strings.size()) val = shared_strings[idx];
                    }
                }
                cells.push_back(trim_ws(val));
                c_pos = end_c + 4;
            }
            if (!cells.empty()) {
                std::string line = "| ";
                for (const auto& c : cells) line += c + " | ";
                line.pop_back();
                sheet_md += line + "\n";
                if (first_row) {
                    std::string sep = "|";
                    for (std::size_t k = 0; k < cells.size(); ++k) sep += " --- |";
                    sheet_md += sep + "\n";
                    first_row = false;
                }
            }
            row_pos = end_row + 6;
        }
        doc.sections.push_back(DocumentSection{
            .index = sh.num,
            .title = sheet_title,
            .text = trim_ws(sheet_md)
        });
        full_md += sheet_md + "\n";
    }
    doc.text = trim_ws(full_md);
}

[[nodiscard]] inline auto extract_pdf_apple(std::span<const std::byte> pdf_bytes, Document& doc) -> bool {
#if defined(__APPLE__)
    const auto& rt = apple_nl_runtime();
    if (!rt.get_class || !rt.sel_register || !rt.msg_send) return false;
    void* nsDataCls = rt.get_class("NSData");
    void* pdfDocCls = rt.get_class("PDFDocument");
    if (!nsDataCls || !pdfDocCls) return false;

    void* selDataWithBytes = rt.sel_register("dataWithBytes:length:");
    void* selAlloc = rt.sel_register("alloc");
    void* selInitWithData = rt.sel_register("initWithData:");
    void* selPageCount = rt.sel_register("pageCount");
    void* selPageAtIndex = rt.sel_register("pageAtIndex:");
    void* selString = rt.sel_register("string");
    void* selUTF8 = rt.sel_register("UTF8String");

    using FnDataBytes = void*(*)(void*, void*, const void*, unsigned long);
    using FnAlloc = void*(*)(void*, void*);
    using FnInitData = void*(*)(void*, void*, void*);
    using FnPageCount = unsigned long(*)(void*, void*);
    using FnPageAt = void*(*)(void*, void*, unsigned long);
    using FnString = void*(*)(void*, void*);
    using FnUTF8 = const char*(*)(void*, void*);

    void* nsData = reinterpret_cast<FnDataBytes>(rt.msg_send)(nsDataCls, selDataWithBytes, pdf_bytes.data(), pdf_bytes.size());
    if (!nsData) return false;
    void* rawDoc = reinterpret_cast<FnAlloc>(rt.msg_send)(pdfDocCls, selAlloc);
    if (!rawDoc) return false;
    void* pdfDoc = reinterpret_cast<FnInitData>(rt.msg_send)(rawDoc, selInitWithData, nsData);
    if (!pdfDoc) return false;

    unsigned long count = reinterpret_cast<FnPageCount>(rt.msg_send)(pdfDoc, selPageCount);
    if (count == 0) return false;

    std::string full_md;
    for (unsigned long i = 0; i < count; ++i) {
        void* page = reinterpret_cast<FnPageAt>(rt.msg_send)(pdfDoc, selPageAtIndex, i);
        if (!page) continue;
        void* nsStr = reinterpret_cast<FnString>(rt.msg_send)(page, selString);
        std::string page_txt;
        if (nsStr) {
            const char* cstr = reinterpret_cast<FnUTF8>(rt.msg_send)(nsStr, selUTF8);
            if (cstr) page_txt = trim_ws(cstr);
        }
        if (!page_txt.empty()) {
            std::string title = std::format("Page {}", i + 1);
            doc.sections.push_back(DocumentSection{
                .index = static_cast<std::int32_t>(i + 1),
                .title = title,
                .text = page_txt
            });
            if (!full_md.empty()) full_md += "\n\n";
            full_md += std::format("## {}\n{}", title, page_txt);
        }
    }
    if (!doc.sections.empty()) {
        doc.text = trim_ws(full_md);
        return true;
    }
#else
    (void)pdf_bytes;
    (void)doc;
#endif
    return false;
}

inline void encode_utf8_codepoint(std::uint32_t cp, std::string& out) {
    if (cp <= 0x7Fu) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | ((cp >> 6) & 0x1Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0xFFFFu) {
        out.push_back(static_cast<char>(0xE0u | ((cp >> 12) & 0x0Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0x10FFFFu) {
        out.push_back(static_cast<char>(0xF0u | ((cp >> 18) & 0x07u)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

[[nodiscard]] inline auto hex_nibble(char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

[[nodiscard]] inline auto parse_hex_uint(std::string_view hex) -> std::uint32_t {
    std::uint32_t val = 0;
    for (char c : hex) {
        int n = hex_nibble(c);
        if (n >= 0) val = (val << 4) | static_cast<std::uint32_t>(n);
    }
    return val;
}

[[nodiscard]] inline auto decode_hex_utf16be_or_bytes(std::string_view hex) -> std::string {
    std::vector<std::uint8_t> bytes;
    int hi = -1;
    for (char c : hex) {
        int n = hex_nibble(c);
        if (n < 0) continue;
        if (hi < 0) {
            hi = n;
        } else {
            bytes.push_back(static_cast<std::uint8_t>((hi << 4) | n));
            hi = -1;
        }
    }
    if (hi >= 0) bytes.push_back(static_cast<std::uint8_t>(hi << 4));
    if (bytes.empty()) return "";

    std::string out;
    if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
        for (std::size_t i = 2; i + 1 < bytes.size(); i += 2) {
            std::uint32_t cp = (static_cast<std::uint32_t>(bytes[i]) << 8) | bytes[i + 1];
            if (cp >= 0xD800u && cp <= 0xDBFFu && i + 3 < bytes.size()) {
                std::uint32_t low = (static_cast<std::uint32_t>(bytes[i + 2]) << 8) | bytes[i + 3];
                if (low >= 0xDC00u && low <= 0xDFFFu) {
                    cp = 0x10000u + (((cp - 0xD800u) << 10) | (low - 0xDC00u));
                    i += 2;
                }
            }
            encode_utf8_codepoint(cp, out);
        }
        return out;
    }
    if (bytes.size() % 2 == 0) {
        bool has_high_zero = false;
        for (std::size_t i = 0; i < bytes.size(); i += 2) {
            if (bytes[i] == 0 && bytes[i + 1] >= 0x20) { has_high_zero = true; break; }
        }
        if (has_high_zero) {
            for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
                std::uint32_t cp = (static_cast<std::uint32_t>(bytes[i]) << 8) | bytes[i + 1];
                encode_utf8_codepoint(cp, out);
            }
            return out;
        }
    }
    for (std::uint8_t b : bytes) {
        if (b >= 0x20 || b == '\n' || b == '\r' || b == '\t') out.push_back(static_cast<char>(b));
    }
    return out;
}

inline void parse_pdf_cmap(std::string_view stream_text, std::unordered_map<std::uint32_t, std::string>& cmap) {
    auto next_hex_token = [&](std::size_t& p, std::size_t limit) -> std::optional<std::string_view> {
        auto lt = stream_text.find('<', p);
        if (lt == std::string_view::npos || lt >= limit) return std::nullopt;
        auto gt = stream_text.find('>', lt + 1);
        if (gt == std::string_view::npos || gt > limit) return std::nullopt;
        p = gt + 1;
        return stream_text.substr(lt + 1, gt - lt - 1);
    };

    std::size_t pos = 0;
    while ((pos = stream_text.find("beginbfchar", pos)) != std::string_view::npos) {
        pos += 11;
        auto end_pos = stream_text.find("endbfchar", pos);
        if (end_pos == std::string_view::npos) break;
        std::size_t scan = pos;
        while (scan < end_pos) {
            auto src_hex = next_hex_token(scan, end_pos);
            if (!src_hex) break;
            auto dst_hex = next_hex_token(scan, end_pos);
            if (!dst_hex) break;
            std::uint32_t cid = parse_hex_uint(*src_hex);
            std::uint32_t cp = parse_hex_uint(*dst_hex);
            std::string utf8;
            if ( dst_hex->size() <= 4 && cp > 0 ) {
                encode_utf8_codepoint(cp, utf8);
            } else {
                utf8 = decode_hex_utf16be_or_bytes(*dst_hex);
            }
            if (!utf8.empty()) cmap[cid] = std::move(utf8);
        }
        pos = end_pos + 9;
    }

    pos = 0;
    while ((pos = stream_text.find("beginbfrange", pos)) != std::string_view::npos) {
        pos += 12;
        auto end_pos = stream_text.find("endbfrange", pos);
        if (end_pos == std::string_view::npos) break;
        std::size_t scan = pos;
        while (scan < end_pos) {
            auto start_hex = next_hex_token(scan, end_pos);
            if (!start_hex) break;
            auto last_hex = next_hex_token(scan, end_pos);
            if (!last_hex) break;
            auto dst_hex = next_hex_token(scan, end_pos);
            if (!dst_hex) break;
            std::uint32_t cid_start = parse_hex_uint(*start_hex);
            std::uint32_t cid_end = parse_hex_uint(*last_hex);
            std::uint32_t cp_start = parse_hex_uint(*dst_hex);
            if (cid_end >= cid_start && (cid_end - cid_start) < 2048) {
                for (std::uint32_t c = cid_start; c <= cid_end; ++c) {
                    std::string utf8;
                    encode_utf8_codepoint(cp_start + (c - cid_start), utf8);
                    cmap[c] = std::move(utf8);
                }
            }
        }
        pos = end_pos + 10;
    }
}

[[nodiscard]] inline auto decode_pdf_hex_string(
    std::string_view hex,
    const std::unordered_map<std::uint32_t, std::string>& cmap
) -> std::string {
    std::string clean;
    clean.reserve(hex.size());
    for (char c : hex) {
        if (hex_nibble(c) >= 0) clean.push_back(c);
    }
    if (clean.empty()) return "";
    if (clean.starts_with("FEFF") || clean.starts_with("feff")) {
        return decode_hex_utf16be_or_bytes(clean);
    }
    if (!cmap.empty()) {
        std::string out;
        if (clean.size() % 4 == 0) {
            bool all_mapped = true;
            for (std::size_t i = 0; i < clean.size(); i += 4) {
                std::uint32_t cid = parse_hex_uint(std::string_view(clean).substr(i, 4));
                if (auto it = cmap.find(cid); it != cmap.end()) {
                    out += it->second;
                } else {
                    all_mapped = false;
                    break;
                }
            }
            if (all_mapped && !out.empty()) return out;
        }
        out.clear();
        if (clean.size() % 2 == 0) {
            bool any_mapped = false;
            for (std::size_t i = 0; i < clean.size(); i += 2) {
                std::uint32_t cid = parse_hex_uint(std::string_view(clean).substr(i, 2));
                if (auto it = cmap.find(cid); it != cmap.end()) {
                    out += it->second;
                    any_mapped = true;
                } else if (cid >= 0x20 && cid <= 0x7E) {
                    out.push_back(static_cast<char>(cid));
                }
            }
            if (any_mapped && !out.empty()) return out;
        }
    }
    return decode_hex_utf16be_or_bytes(clean);
}

[[nodiscard]] inline auto decode_pdf_string_literal(
    std::string_view lit,
    const std::unordered_map<std::uint32_t, std::string>& cmap = {}
) -> std::string {
    std::string raw;
    raw.reserve(lit.size());
    for (std::size_t i = 0; i < lit.size(); ++i) {
        if (lit[i] == '\\' && i + 1 < lit.size()) {
            char next = lit[++i];
            if (next == 'n') raw.push_back('\n');
            else if (next == 'r') raw.push_back('\r');
            else if (next == 't') raw.push_back('\t');
            else if (next == '(' || next == ')' || next == '\\') raw.push_back(next);
            else if (next >= '0' && next <= '7') {
                int oct = next - '0';
                if (i + 1 < lit.size() && lit[i + 1] >= '0' && lit[i + 1] <= '7') oct = oct * 8 + (lit[++i] - '0');
                if (i + 1 < lit.size() && lit[i + 1] >= '0' && lit[i + 1] <= '7') oct = oct * 8 + (lit[++i] - '0');
                raw.push_back(static_cast<char>(oct));
            } else {
                raw.push_back(next);
            }
        } else {
            raw.push_back(lit[i]);
        }
    }
    if (!cmap.empty()) {
        std::string mapped;
        bool used_cmap = false;
        for (unsigned char c : raw) {
            if (auto it = cmap.find(static_cast<std::uint32_t>(c)); it != cmap.end()) {
                mapped += it->second;
                used_cmap = true;
            } else {
                mapped.push_back(static_cast<char>(c));
            }
        }
        if (used_cmap) return mapped;
    }
    return raw;
}

[[nodiscard]] inline auto parse_pdf_content_stream(
    std::string_view stream_text,
    const std::unordered_map<std::uint32_t, std::string>& cmap = {}
) -> std::string {
    if (stream_text.find("beginbfchar") != std::string_view::npos ||
        stream_text.find("beginbfrange") != std::string_view::npos) {
        return "";
    }
    std::string page_out;
    std::size_t pos = 0;
    while (pos < stream_text.size()) {
        auto bt = stream_text.find("BT", pos);
        if (bt == std::string_view::npos) break;
        if (bt > 0 && std::isalnum(static_cast<unsigned char>(stream_text[bt - 1]))) {
            pos = bt + 2;
            continue;
        }
        auto et = stream_text.find("ET", bt + 2);
        if (et == std::string_view::npos) et = stream_text.size();
        std::string_view blk = stream_text.substr(bt + 2, et - (bt + 2));

        std::string blk_text;
        bool in_tj_array = false;
        std::size_t i = 0;
        while (i < blk.size()) {
            if (blk[i] == '[') {
                in_tj_array = true;
                ++i;
            } else if (blk[i] == ']') {
                in_tj_array = false;
                ++i;
            } else if (blk[i] == '(') {
                int depth = 1;
                std::size_t start = ++i;
                while (i < blk.size() && depth > 0) {
                    if (blk[i] == '\\') { i += 2; continue; }
                    if (blk[i] == '(') ++depth;
                    else if (blk[i] == ')') --depth;
                    if (depth > 0) ++i;
                }
                blk_text += decode_pdf_string_literal(blk.substr(start, i - start), cmap);
                if (i < blk.size()) ++i;
            } else if (blk[i] == '<' && (i + 1 >= blk.size() || blk[i + 1] != '<')) {
                auto gt = blk.find('>', i + 1);
                if (gt == std::string_view::npos) break;
                blk_text += decode_pdf_hex_string(blk.substr(i + 1, gt - i - 1), cmap);
                i = gt + 1;
            } else if (in_tj_array && (blk[i] == '-' || std::isdigit(static_cast<unsigned char>(blk[i])))) {
                std::size_t num_start = i;
                if (blk[i] == '-') ++i;
                while (i < blk.size() && (std::isdigit(static_cast<unsigned char>(blk[i])) || blk[i] == '.')) ++i;
                double kern = std::strtod(std::string(blk.substr(num_start, i - num_start)).c_str(), nullptr);
                if (kern <= -120.0 && !blk_text.empty() && blk_text.back() != ' ' && blk_text.back() != '\n') {
                    blk_text.push_back(' ');
                }
            } else if (blk.substr(i).starts_with("Td") || blk.substr(i).starts_with("TD") || blk.substr(i).starts_with("T*")) {
                if (!blk_text.empty() && blk_text.back() != '\n') blk_text.push_back('\n');
                i += 2;
            } else {
                ++i;
            }
        }
        blk_text = trim_ws(blk_text);
        if (!blk_text.empty()) {
            if (!page_out.empty()) page_out += "\n";
            page_out += blk_text;
        }
        pos = et + 2;
    }
    return page_out;
}

[[nodiscard]] inline auto extract_pdf_linux(std::span<const std::byte> pdf_bytes, Document& doc) -> bool {
#if defined(__linux__)
    static int has_pdftotext = -1;
    if (has_pdftotext < 0) {
        has_pdftotext = (std::system("which pdftotext >/dev/null 2>&1") == 0) ? 1 : 0;
    }
    if (has_pdftotext != 1) return false;

    static std::uint64_t seq = 0;
    std::string tmp_path = std::format("/tmp/cfire_pdf_{}_{}.pdf", static_cast<long>(::getpid()), ++seq);
    if (!cfire::fs::write_bytes(tmp_path, pdf_bytes)) return false;

    std::string cmd = std::format("pdftotext -layout '{}' - 2>/dev/null", tmp_path);
    std::string full_out;
    if (FILE* pipe = ::popen(cmd.c_str(), "r")) {
        char buf[4096];
        while (std::size_t n = std::fread(buf, 1, sizeof(buf), pipe)) {
            full_out.append(buf, n);
        }
        ::pclose(pipe);
    }
    (void)cfire::fs::remove(tmp_path);

    if (trim_ws(full_out).empty()) return false;
    std::string full_md;
    std::size_t start = 0;
    std::int32_t page_idx = 1;
    while (start < full_out.size()) {
        auto ff = full_out.find('\f', start);
        if (ff == std::string::npos) ff = full_out.size();
        std::string page_txt = trim_ws(std::string_view(full_out).substr(start, ff - start));
        if (!page_txt.empty()) {
            std::string title = std::format("Page {}", page_idx);
            doc.sections.push_back(DocumentSection{.index = page_idx++, .title = title, .text = page_txt});
            if (!full_md.empty()) full_md += "\n\n";
            full_md += std::format("## {}\n{}", title, page_txt);
        }
        start = ff + 1;
    }
    doc.text = trim_ws(full_md);
    return !doc.text.empty();
#else
    (void)pdf_bytes;
    (void)doc;
    return false;
#endif
}

inline void parse_pdf_portable(std::span<const std::byte> pdf_bytes, Document& doc) {
    doc.format = "pdf";
    if (extract_pdf_apple(pdf_bytes, doc)) return;
    if (extract_pdf_linux(pdf_bytes, doc)) return;

    std::string_view raw(reinterpret_cast<const char*>(pdf_bytes.data()), pdf_bytes.size());
    std::vector<std::string> decoded_streams;
    std::unordered_map<std::uint32_t, std::string> cmap;

    std::size_t pos = 0;
    while ((pos = raw.find("stream", pos)) != std::string_view::npos) {
        if (pos > 0 && raw[pos - 1] == 'd') { // "endstream"
            pos += 6;
            continue;
        }
        std::size_t dict_start = (pos > 256) ? (pos - 256) : 0;
        std::string_view dict_window = raw.substr(dict_start, pos - dict_start);
        bool is_flate = dict_window.find("/FlateDecode") != std::string_view::npos;

        std::size_t data_start = pos + 6;
        if (data_start < raw.size() && raw[data_start] == '\r') ++data_start;
        if (data_start < raw.size() && raw[data_start] == '\n') ++data_start;

        auto end_s = raw.find("endstream", data_start);
        if (end_s == std::string_view::npos) break;
        std::size_t data_end = end_s;
        while (data_end > data_start && (raw[data_end - 1] == '\n' || raw[data_end - 1] == '\r')) --data_end;

        std::string decoded_stream;
        if (is_flate && data_end > data_start) {
            auto dec = inflate_zlib_or_raw(pdf_bytes.subspan(data_start, data_end - data_start));
            decoded_stream.assign(reinterpret_cast<const char*>(dec.data()), dec.size());
        } else if (data_end > data_start) {
            decoded_stream.assign(raw.data() + data_start, data_end - data_start);
        }

        if (decoded_stream.find("beginbfchar") != std::string::npos ||
            decoded_stream.find("beginbfrange") != std::string::npos) {
            parse_pdf_cmap(decoded_stream, cmap);
        } else if (!decoded_stream.empty()) {
            decoded_streams.push_back(std::move(decoded_stream));
        }
        pos = end_s + 9;
    }

    std::int32_t page_num = 1;
    std::string full_md;
    for (const auto& stream_str : decoded_streams) {
        std::string page_txt = parse_pdf_content_stream(stream_str, cmap);
        if (!page_txt.empty()) {
            std::string title = std::format("Page {}", page_num);
            doc.sections.push_back(DocumentSection{
                .index = page_num++,
                .title = title,
                .text = page_txt
            });
            if (!full_md.empty()) full_md += "\n\n";
            full_md += std::format("## {}\n{}", title, page_txt);
        }
    }
    doc.text = trim_ws(full_md);
}

inline void parse_html_document(std::string_view html, Document& doc) {
    doc.format = "html";
    std::string cleaned;
    cleaned.reserve(html.size());

    // Strip <script>, <style>, and <!-- comments -->
    std::size_t i = 0;
    while (i < html.size()) {
        if (html.substr(i).starts_with("<!--")) {
            auto end_c = html.find("-->", i + 4);
            i = (end_c == std::string_view::npos) ? html.size() : (end_c + 3);
            continue;
        }
        if (html[i] == '<') {
            auto gt = html.find('>', i + 1);
            if (gt == std::string_view::npos) break;
            std::string tag_raw(html.substr(i + 1, gt - i - 1));
            std::string tag_lower;
            for (unsigned char c : tag_raw) {
                if (std::isspace(c) || c == '/') {
                    if (!tag_lower.empty()) break;
                    if (c == '/' && tag_lower.empty()) tag_lower.push_back('/');
                } else {
                    tag_lower.push_back(static_cast<char>(std::tolower(c)));
                }
            }
            if (tag_lower == "script" || tag_lower == "style") {
                std::string close_needle = "</" + tag_lower;
                std::size_t scan = gt + 1;
                while (scan < html.size()) {
                    if (html[scan] == '<' && scan + close_needle.size() <= html.size()) {
                        bool match = true;
                        for (std::size_t k = 0; k < close_needle.size(); ++k) {
                            if (std::tolower(static_cast<unsigned char>(html[scan + k])) != close_needle[k]) {
                                match = false;
                                break;
                            }
                        }
                        if (match) {
                            auto close_gt = html.find('>', scan);
                            scan = (close_gt == std::string_view::npos) ? html.size() : (close_gt + 1);
                            break;
                        }
                    }
                    ++scan;
                }
                i = scan;
                continue;
            }
            if (tag_lower.size() == 2 && tag_lower[0] == 'h' && tag_lower[1] >= '1' && tag_lower[1] <= '6') {
                cleaned += "\n\n" + std::string(static_cast<std::size_t>(tag_lower[1] - '0'), '#') + " ";
            } else if (tag_lower.size() == 3 && tag_lower[0] == '/' && tag_lower[1] == 'h' && tag_lower[2] >= '1' && tag_lower[2] <= '6') {
                cleaned += "\n\n";
            } else if (tag_lower == "li") {
                cleaned += "\n- ";
            } else if (tag_lower == "p" || tag_lower == "/p" || tag_lower == "br" || tag_lower == "div" || tag_lower == "/div" || tag_lower == "/tr") {
                cleaned += "\n";
            } else if (tag_lower == "td" || tag_lower == "th") {
                cleaned += " | ";
            }
            i = gt + 1;
            continue;
        }
        cleaned.push_back(html[i++]);
    }

    std::string unescaped = xml_unescape(cleaned);
    // Split into sections by Markdown headings
    std::string full_md;
    std::string cur_title = "Document";
    std::string cur_body;
    std::int32_t sec_idx = 1;
    std::size_t line_start = 0;
    while (line_start < unescaped.size()) {
        auto nl = unescaped.find('\n', line_start);
        if (nl == std::string::npos) nl = unescaped.size();
        std::string line = trim_ws(std::string_view(unescaped).substr(line_start, nl - line_start));
        line_start = nl + 1;
        if (line.empty()) continue;
        if (line.starts_with("#")) {
            if (!trim_ws(cur_body).empty()) {
                doc.sections.push_back(DocumentSection{.index = sec_idx++, .title = cur_title, .text = trim_ws(cur_body)});
                cur_body.clear();
            }
            std::size_t h_end = 0;
            while (h_end < line.size() && line[h_end] == '#') ++h_end;
            cur_title = trim_ws(std::string_view(line).substr(h_end));
        }
        cur_body += line + "\n";
        full_md += line + "\n";
    }
    if (!trim_ws(cur_body).empty()) {
        doc.sections.push_back(DocumentSection{.index = sec_idx, .title = cur_title, .text = trim_ws(cur_body)});
    }
    doc.text = trim_ws(full_md);
}

inline void parse_markdown_sections(std::string_view md, Document& doc) {
    doc.text = trim_ws(md);
    std::string cur_title = "Section 1";
    std::string cur_body;
    std::int32_t sec_idx = 1;
    std::size_t pos = 0;
    while (pos < md.size()) {
        auto nl = md.find('\n', pos);
        if (nl == std::string_view::npos) nl = md.size();
        std::string_view line = md.substr(pos, nl - pos);
        pos = nl + 1;
        std::string trimmed = trim_ws(line);
        if (trimmed.starts_with("#")) {
            if (!trim_ws(cur_body).empty()) {
                doc.sections.push_back(DocumentSection{.index = sec_idx++, .title = cur_title, .text = trim_ws(cur_body)});
                cur_body.clear();
            }
            std::size_t h = 0;
            while (h < trimmed.size() && trimmed[h] == '#') ++h;
            cur_title = trim_ws(std::string_view(trimmed).substr(h));
        }
        cur_body.append(line.data(), line.size());
        cur_body.push_back('\n');
    }
    if (!trim_ws(cur_body).empty()) {
        doc.sections.push_back(DocumentSection{.index = sec_idx, .title = cur_title, .text = trim_ws(cur_body)});
    }
}

[[nodiscard]] inline auto ocr_apple_vision(std::span<const std::byte> img_bytes) -> std::string {
#if defined(__APPLE__)
    const auto& rt = apple_nl_runtime();
    if (!rt.get_class || !rt.sel_register || !rt.msg_send) return "";
    void* nsDataCls = rt.get_class("NSData");
    void* nsDictCls = rt.get_class("NSDictionary");
    void* nsArrayCls = rt.get_class("NSArray");
    void* vnHandlerCls = rt.get_class("VNImageRequestHandler");
    void* vnRecReqCls = rt.get_class("VNRecognizeTextRequest");
    if (!nsDataCls || !nsDictCls || !nsArrayCls || !vnHandlerCls || !vnRecReqCls) return "";

    void* selDataWithBytes = rt.sel_register("dataWithBytes:length:");
    void* selDict = rt.sel_register("dictionary");
    void* selAlloc = rt.sel_register("alloc");
    void* selInit = rt.sel_register("init");
    void* selInitDataOpts = rt.sel_register("initWithData:options:");
    void* selArrayWithObj = rt.sel_register("arrayWithObject:");
    void* selPerform = rt.sel_register("performRequests:error:");
    void* selResults = rt.sel_register("results");
    void* selCount = rt.sel_register("count");
    void* selObjAt = rt.sel_register("objectAtIndex:");
    void* selTopCand = rt.sel_register("topCandidates:");
    void* selFirstObj = rt.sel_register("firstObject");
    void* selString = rt.sel_register("string");
    void* selUTF8 = rt.sel_register("UTF8String");

    using FnDataBytes = void*(*)(void*, void*, const void*, unsigned long);
    using FnZero = void*(*)(void*, void*);
    using FnInitDataOpts = void*(*)(void*, void*, void*, void*);
    using FnArrObj = void*(*)(void*, void*, void*);
    using FnPerform = signed char(*)(void*, void*, void*, void**);
    using FnCount = unsigned long(*)(void*, void*);
    using FnObjAt = void*(*)(void*, void*, unsigned long);
    using FnUTF8 = const char*(*)(void*, void*);

    void* nsData = reinterpret_cast<FnDataBytes>(rt.msg_send)(nsDataCls, selDataWithBytes, img_bytes.data(), img_bytes.size());
    void* emptyDict = reinterpret_cast<FnZero>(rt.msg_send)(nsDictCls, selDict);
    if (!nsData || !emptyDict) return "";

    void* rawHandler = reinterpret_cast<FnZero>(rt.msg_send)(vnHandlerCls, selAlloc);
    void* handler = rawHandler ? reinterpret_cast<FnInitDataOpts>(rt.msg_send)(rawHandler, selInitDataOpts, nsData, emptyDict) : nullptr;
    void* rawReq = reinterpret_cast<FnZero>(rt.msg_send)(vnRecReqCls, selAlloc);
    void* req = rawReq ? reinterpret_cast<FnZero>(rt.msg_send)(rawReq, selInit) : nullptr;
    if (!handler || !req) return "";

    void* reqArray = reinterpret_cast<FnArrObj>(rt.msg_send)(nsArrayCls, selArrayWithObj, req);
    void* err = nullptr;
    signed char ok = reinterpret_cast<FnPerform>(rt.msg_send)(handler, selPerform, reqArray, &err);
    if (!ok) return "";

    void* results = reinterpret_cast<FnZero>(rt.msg_send)(req, selResults);
    if (!results) return "";
    unsigned long n = reinterpret_cast<FnCount>(rt.msg_send)(results, selCount);
    std::string out;
    for (unsigned long i = 0; i < n; ++i) {
        void* obs = reinterpret_cast<FnObjAt>(rt.msg_send)(results, selObjAt, i);
        if (!obs) continue;
        void* cands = reinterpret_cast<FnObjAt>(rt.msg_send)(obs, selTopCand, 1UL);
        if (!cands) continue;
        void* top = reinterpret_cast<FnZero>(rt.msg_send)(cands, selFirstObj);
        if (!top) continue;
        void* nsStr = reinterpret_cast<FnZero>(rt.msg_send)(top, selString);
        if (!nsStr) continue;
        const char* utf8 = reinterpret_cast<FnUTF8>(rt.msg_send)(nsStr, selUTF8);
        if (utf8 && *utf8) {
            if (!out.empty()) out += "\n";
            out += utf8;
        }
    }
    return out;
#else
    (void)img_bytes;
    return "";
#endif
}

[[nodiscard]] inline auto extract_image_text_metadata(std::span<const std::byte> bytes) -> std::string {
    std::string out;
    // 1. Check PNG tEXt / iTXt chunks
    if (bytes.size() > 8 && static_cast<std::uint8_t>(bytes[0]) == 0x89 && static_cast<std::uint8_t>(bytes[1]) == 'P' &&
        static_cast<std::uint8_t>(bytes[2]) == 'N' && static_cast<std::uint8_t>(bytes[3]) == 'G') {
        std::size_t pos = 8;
        while (pos + 12 <= bytes.size()) {
            std::uint32_t len = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[pos])) << 24) |
                                (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[pos + 1])) << 16) |
                                (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[pos + 2])) << 8) |
                                static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[pos + 3]));
            std::string_view type(reinterpret_cast<const char*>(bytes.data() + pos + 4), 4);
            if (pos + 12 + len > bytes.size()) break;
            if (type == "tEXt" || type == "iTXt") {
                std::string_view chunk(reinterpret_cast<const char*>(bytes.data() + pos + 8), len);
                auto nul = chunk.find('\0');
                std::string_view val = (nul != std::string_view::npos) ? chunk.substr(nul + 1) : chunk;
                while (!val.empty() && val.front() == '\0') val.remove_prefix(1);
                if (!val.empty()) {
                    if (!out.empty()) out += "\n";
                    out.append(val.data(), val.size());
                }
            }
            pos += 12 + len;
        }
    }
    // 2. Check JPEG COM (0xFF 0xFE) comment segments
    if (bytes.size() > 4 && static_cast<std::uint8_t>(bytes[0]) == 0xFF && static_cast<std::uint8_t>(bytes[1]) == 0xD8) {
        std::size_t pos = 2;
        while (pos + 4 <= bytes.size() && static_cast<std::uint8_t>(bytes[pos]) == 0xFF) {
            auto marker = static_cast<std::uint8_t>(bytes[pos + 1]);
            if (marker == 0xDA || marker == 0xD9) break;
            std::uint16_t seg_len = static_cast<std::uint16_t>((static_cast<std::uint8_t>(bytes[pos + 2]) << 8) |
                                                               static_cast<std::uint8_t>(bytes[pos + 3]));
            if (seg_len < 2 || pos + 2 + seg_len > bytes.size()) break;
            if (marker == 0xFE && seg_len > 2) {
                std::string_view com(reinterpret_cast<const char*>(bytes.data() + pos + 4), seg_len - 2);
                if (!com.empty()) {
                    if (!out.empty()) out += "\n";
                    out.append(com.data(), com.size());
                }
            }
            pos += 2 + seg_len;
        }
    }
    return out;
}

inline void parse_odf_zip(
    const std::unordered_map<std::string, std::string>& entries,
    std::string_view mime_type,
    Document& doc
) {
    if (mime_type.find("presentation") != std::string::npos) doc.format = "odp";
    else if (mime_type.find("spreadsheet") != std::string::npos) doc.format = "ods";
    else doc.format = "odt";

    auto it = entries.find("content.xml");
    if (it == entries.end()) return;
    std::string_view xml = it->second;

    if (doc.format == "odp") {
        std::size_t pos = 0;
        std::int32_t slide_idx = 1;
        std::string full_md;
        while ((pos = xml.find("<draw:page", pos)) != std::string_view::npos) {
            auto gt = xml.find('>', pos);
            if (gt == std::string_view::npos) break;
            if (xml[gt - 1] == '/') { pos = gt + 1; continue; }
            auto end_page = xml.find("</draw:page>", gt + 1);
            if (end_page == std::string_view::npos) break;
            std::string_view page_xml = xml.substr(gt + 1, end_page - (gt + 1));

            std::string slide_txt;
            std::size_t p_pos = 0;
            while ((p_pos = page_xml.find("<text:p", p_pos)) != std::string_view::npos) {
                auto p_gt = page_xml.find('>', p_pos);
                if (p_gt == std::string_view::npos) break;
                if (page_xml[p_gt - 1] == '/') { p_pos = p_gt + 1; continue; }
                auto p_end = page_xml.find("</text:p>", p_gt + 1);
                if (p_end == std::string_view::npos) break;
                std::string_view inner = page_xml.substr(p_gt + 1, p_end - (p_gt + 1));
                std::string line;
                std::size_t k = 0;
                while (k < inner.size()) {
                    if (inner[k] == '<') {
                        auto t_end = inner.find('>', k);
                        if (t_end == std::string_view::npos) break;
                        std::string_view tag = inner.substr(k + 1, t_end - k - 1);
                        if (tag.starts_with("text:s")) line.push_back(' ');
                        else if (tag.starts_with("text:tab")) line.push_back('\t');
                        else if (tag.starts_with("text:line-break")) line.push_back('\n');
                        k = t_end + 1;
                    } else {
                        line.push_back(inner[k++]);
                    }
                }
                line = trim_ws(xml_unescape(line));
                if (!line.empty()) {
                    if (!slide_txt.empty()) slide_txt += "\n";
                    slide_txt += line;
                }
                p_pos = p_end + 9;
            }
            if (!slide_txt.empty()) {
                std::string title = std::format("Slide {}", slide_idx);
                doc.sections.push_back(DocumentSection{.index = slide_idx++, .title = title, .text = slide_txt});
                if (!full_md.empty()) full_md += "\n\n";
                full_md += std::format("## {}\n{}", title, slide_txt);
            }
            pos = end_page + 12;
        }
        doc.text = trim_ws(full_md);
        return;
    }

    if (doc.format == "ods") {
        std::size_t pos = 0;
        std::int32_t sheet_idx = 1;
        std::string full_md;
        while ((pos = xml.find("<table:table", pos)) != std::string_view::npos) {
            if (xml.substr(pos).starts_with("<table:table-")) { pos += 13; continue; }
            auto gt = xml.find('>', pos);
            if (gt == std::string_view::npos) break;
            auto end_tbl = xml.find("</table:table>", gt + 1);
            if (end_tbl == std::string_view::npos) break;
            std::string_view tbl_xml = xml.substr(gt + 1, end_tbl - (gt + 1));

            std::string sheet_md;
            std::size_t r_pos = 0;
            std::size_t row_idx = 0;
            while ((r_pos = tbl_xml.find("<table:table-row", r_pos)) != std::string_view::npos) {
                auto r_gt = tbl_xml.find('>', r_pos);
                if (r_gt == std::string_view::npos) break;
                if (tbl_xml[r_gt - 1] == '/') { r_pos = r_gt + 1; continue; }
                auto r_end = tbl_xml.find("</table:table-row>", r_gt + 1);
                if (r_end == std::string_view::npos) break;
                std::string_view row_xml = tbl_xml.substr(r_gt + 1, r_end - (r_gt + 1));

                std::vector<std::string> cells;
                std::size_t c_pos = 0;
                while ((c_pos = row_xml.find("<table:table-cell", c_pos)) != std::string_view::npos) {
                    auto c_gt = row_xml.find('>', c_pos);
                    if (c_gt == std::string_view::npos) break;
                    if (row_xml[c_gt - 1] == '/') { cells.emplace_back(""); c_pos = c_gt + 1; continue; }
                    auto c_end = row_xml.find("</table:table-cell>", c_gt + 1);
                    if (c_end == std::string_view::npos) break;
                    std::string_view cell_xml = row_xml.substr(c_gt + 1, c_end - (c_gt + 1));
                    std::string cell_txt;
                    std::size_t k = 0;
                    while (k < cell_xml.size()) {
                        if (cell_xml[k] == '<') {
                            auto t_end = cell_xml.find('>', k);
                            if (t_end == std::string_view::npos) break;
                            k = t_end + 1;
                        } else {
                            cell_txt.push_back(cell_xml[k++]);
                        }
                    }
                    cells.push_back(trim_ws(xml_unescape(cell_txt)));
                    c_pos = c_end + 19;
                }
                if (!cells.empty()) {
                    sheet_md += "| ";
                    for (const auto& c : cells) sheet_md += c + " | ";
                    sheet_md.pop_back();
                    sheet_md += "\n";
                    if (row_idx == 0) {
                        sheet_md += "|";
                        for (std::size_t k = 0; k < cells.size(); ++k) sheet_md += " --- |";
                        sheet_md += "\n";
                    }
                    ++row_idx;
                }
                r_pos = r_end + 18;
            }
            sheet_md = trim_ws(sheet_md);
            if (!sheet_md.empty()) {
                std::string title = std::format("Sheet {}", sheet_idx);
                doc.sections.push_back(DocumentSection{.index = sheet_idx++, .title = title, .text = sheet_md});
                if (!full_md.empty()) full_md += "\n\n";
                full_md += std::format("## {}\n{}", title, sheet_md);
            }
            pos = end_tbl + 14;
        }
        doc.text = trim_ws(full_md);
        return;
    }

    // Default: OpenDocument Text (.odt)
    std::string full_md;
    std::string current_title = "Document";
    std::string current_body;
    std::int32_t sec_idx = 1;
    std::size_t pos = 0;
    while (pos < xml.size()) {
        auto next_h = xml.find("<text:h", pos);
        auto next_p = xml.find("<text:p", pos);
        std::size_t tag_pos = std::min(next_h, next_p);
        if (tag_pos == std::string_view::npos) break;
        bool is_heading = (tag_pos == next_h);
        std::string close_tag = is_heading ? "</text:h>" : "</text:p>";

        auto gt = xml.find('>', tag_pos);
        if (gt == std::string_view::npos) break;
        if (xml[gt - 1] == '/') { pos = gt + 1; continue; }
        auto end_el = xml.find(close_tag, gt + 1);
        if (end_el == std::string_view::npos) break;
        std::string_view inner = xml.substr(gt + 1, end_el - (gt + 1));

        std::string para;
        std::size_t k = 0;
        while (k < inner.size()) {
            if (inner[k] == '<') {
                auto t_end = inner.find('>', k);
                if (t_end == std::string_view::npos) break;
                std::string_view tag = inner.substr(k + 1, t_end - k - 1);
                if (tag.starts_with("text:s")) para.push_back(' ');
                else if (tag.starts_with("text:tab")) para.push_back('\t');
                else if (tag.starts_with("text:line-break")) para.push_back('\n');
                k = t_end + 1;
            } else {
                para.push_back(inner[k++]);
            }
        }
        para = trim_ws(xml_unescape(para));
        if (!para.empty()) {
            if (is_heading) {
                if (!trim_ws(current_body).empty()) {
                    doc.sections.push_back(DocumentSection{.index = sec_idx++, .title = current_title, .text = trim_ws(current_body)});
                    current_body.clear();
                }
                current_title = para;
                std::string h_line = "## " + para;
                current_body += h_line + "\n\n";
                full_md += h_line + "\n\n";
            } else {
                current_body += para + "\n\n";
                full_md += para + "\n\n";
            }
        }
        pos = end_el + close_tag.size();
    }
    if (!trim_ws(current_body).empty()) {
        doc.sections.push_back(DocumentSection{.index = sec_idx, .title = current_title, .text = trim_ws(current_body)});
    }
    doc.text = trim_ws(full_md);
}

inline void parse_epub_zip(const std::unordered_map<std::string, std::string>& entries, Document& doc) {
    doc.format = "epub";
    std::vector<std::string> chapter_keys;
    for (const auto& [k, _] : entries) {
        if (k.ends_with(".xhtml") || k.ends_with(".html") || k.ends_with(".htm")) {
            if (k.find("nav") == std::string::npos && k.find("toc") == std::string::npos) {
                chapter_keys.push_back(k);
            }
        }
    }
    std::sort(chapter_keys.begin(), chapter_keys.end());
    std::string full_md;
    std::int32_t ch_idx = 1;
    for (const auto& k : chapter_keys) {
        Document ch_doc;
        parse_html_document(entries.at(k), ch_doc);
        if (!ch_doc.text.empty()) {
            std::string title = (!ch_doc.sections.empty() && ch_doc.sections.front().title != "Document")
                                ? ch_doc.sections.front().title
                                : std::format("Chapter {}", ch_idx);
            doc.sections.push_back(DocumentSection{.index = ch_idx++, .title = title, .text = ch_doc.text});
            if (!full_md.empty()) full_md += "\n\n";
            full_md += ch_doc.text;
        }
    }
    doc.text = trim_ws(full_md);
}

struct DecodedImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba{};
};

[[nodiscard]] inline auto paeth_predictor(int a, int b, int c) noexcept -> std::uint8_t {
    int p = a + b - c;
    int pa = std::abs(p - a);
    int pb = std::abs(p - b);
    int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return static_cast<std::uint8_t>(a);
    if (pb <= pc) return static_cast<std::uint8_t>(b);
    return static_cast<std::uint8_t>(c);
}

[[nodiscard]] inline auto decode_image_pixels(std::span<const std::byte> bytes) -> DecodedImage {
    DecodedImage img;
    // 1. PNG decoder (IHDR + IDAT inflate + scanline unfiltering)
    if (bytes.size() >= 33 && static_cast<std::uint8_t>(bytes[0]) == 0x89 &&
        static_cast<std::uint8_t>(bytes[1]) == 'P' && static_cast<std::uint8_t>(bytes[2]) == 'N' &&
        static_cast<std::uint8_t>(bytes[3]) == 'G') {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint8_t bit_depth = 0;
        std::uint8_t color_type = 0;
        std::uint8_t interlace = 0;
        std::vector<std::byte> idat;

        std::size_t pos = 8;
        while (pos + 12 <= bytes.size()) {
            std::uint32_t len = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[pos])) << 24) |
                                (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[pos + 1])) << 16) |
                                (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[pos + 2])) << 8) |
                                static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[pos + 3]));
            std::string_view type(reinterpret_cast<const char*>(bytes.data() + pos + 4), 4);
            if (pos + 12 + len > bytes.size()) break;
            const std::byte* chunk_data = bytes.data() + pos + 8;
            if (type == "IHDR" && len >= 13) {
                width = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(chunk_data[0])) << 24) |
                        (static_cast<std::uint32_t>(static_cast<std::uint8_t>(chunk_data[1])) << 16) |
                        (static_cast<std::uint32_t>(static_cast<std::uint8_t>(chunk_data[2])) << 8) |
                        static_cast<std::uint32_t>(static_cast<std::uint8_t>(chunk_data[3]));
                height = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(chunk_data[4])) << 24) |
                         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(chunk_data[5])) << 16) |
                         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(chunk_data[6])) << 8) |
                         static_cast<std::uint32_t>(static_cast<std::uint8_t>(chunk_data[7]));
                bit_depth = static_cast<std::uint8_t>(chunk_data[8]);
                color_type = static_cast<std::uint8_t>(chunk_data[9]);
                interlace = static_cast<std::uint8_t>(chunk_data[12]);
            } else if (type == "IDAT") {
                idat.insert(idat.end(), chunk_data, chunk_data + len);
            } else if (type == "IEND") {
                break;
            }
            pos += 12 + len;
        }

        if (width > 0 && height > 0 && width <= 8192 && height <= 8192 && bit_depth == 8 && interlace == 0 && !idat.empty()) {
            std::size_t bpp = (color_type == 6) ? 4 : (color_type == 2) ? 3 : (color_type == 4) ? 2 : (color_type == 0) ? 1 : 0;
            if (bpp > 0) {
                auto raw = inflate_zlib_or_raw(idat);
                std::size_t stride = static_cast<std::size_t>(width) * bpp;
                if (raw.size() >= static_cast<std::size_t>(height) * (1 + stride)) {
                    std::vector<std::uint8_t> recon(static_cast<std::size_t>(height) * stride, 0);
                    for (std::uint32_t y = 0; y < height; ++y) {
                        std::uint8_t filter = static_cast<std::uint8_t>(raw[static_cast<std::size_t>(y) * (1 + stride)]);
                        const std::byte* src_row = raw.data() + static_cast<std::size_t>(y) * (1 + stride) + 1;
                        std::uint8_t* dst_row = recon.data() + static_cast<std::size_t>(y) * stride;
                        const std::uint8_t* prev_row = (y > 0) ? (recon.data() + static_cast<std::size_t>(y - 1) * stride) : nullptr;

                        for (std::size_t x = 0; x < stride; ++x) {
                            std::uint8_t val = static_cast<std::uint8_t>(src_row[x]);
                            std::uint8_t a = (x >= bpp) ? dst_row[x - bpp] : 0;
                            std::uint8_t b = prev_row ? prev_row[x] : 0;
                            std::uint8_t c = (prev_row && x >= bpp) ? prev_row[x - bpp] : 0;
                            switch (filter) {
                                case 0: dst_row[x] = val; break;
                                case 1: dst_row[x] = static_cast<std::uint8_t>(val + a); break;
                                case 2: dst_row[x] = static_cast<std::uint8_t>(val + b); break;
                                case 3: dst_row[x] = static_cast<std::uint8_t>(val + ((static_cast<int>(a) + static_cast<int>(b)) >> 1)); break;
                                case 4: dst_row[x] = static_cast<std::uint8_t>(val + paeth_predictor(a, b, c)); break;
                                default: dst_row[x] = val; break;
                            }
                        }
                    }
                    img.width = width;
                    img.height = height;
                    img.rgba.resize(static_cast<std::size_t>(width) * height * 4);
                    for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height; ++i) {
                        if (bpp == 4) {
                            img.rgba[i * 4 + 0] = recon[i * 4 + 0];
                            img.rgba[i * 4 + 1] = recon[i * 4 + 1];
                            img.rgba[i * 4 + 2] = recon[i * 4 + 2];
                            img.rgba[i * 4 + 3] = recon[i * 4 + 3];
                        } else if (bpp == 3) {
                            img.rgba[i * 4 + 0] = recon[i * 3 + 0];
                            img.rgba[i * 4 + 1] = recon[i * 3 + 1];
                            img.rgba[i * 4 + 2] = recon[i * 3 + 2];
                            img.rgba[i * 4 + 3] = 255;
                        } else if (bpp == 2) {
                            img.rgba[i * 4 + 0] = recon[i * 2];
                            img.rgba[i * 4 + 1] = recon[i * 2];
                            img.rgba[i * 4 + 2] = recon[i * 2];
                            img.rgba[i * 4 + 3] = recon[i * 2 + 1];
                        } else {
                            img.rgba[i * 4 + 0] = recon[i];
                            img.rgba[i * 4 + 1] = recon[i];
                            img.rgba[i * 4 + 2] = recon[i];
                            img.rgba[i * 4 + 3] = 255;
                        }
                    }
                    return img;
                }
            }
        }
    }

    // 2. BMP decoder (24-bit or 32-bit uncompressed)
    if (bytes.size() >= 54 && static_cast<std::uint8_t>(bytes[0]) == 'B' && static_cast<std::uint8_t>(bytes[1]) == 'M') {
        std::uint32_t data_off = read_u32_le(bytes.data() + 10);
        auto w = static_cast<std::int32_t>(read_u32_le(bytes.data() + 18));
        auto h = static_cast<std::int32_t>(read_u32_le(bytes.data() + 22));
        std::uint16_t bpp = read_u16_le(bytes.data() + 28);
        bool flip = (h > 0);
        std::uint32_t abs_h = static_cast<std::uint32_t>(std::abs(h));
        if (w > 0 && abs_h > 0 && w <= 4096 && abs_h <= 4096 && (bpp == 24 || bpp == 32)) {
            std::size_t row_stride = ((static_cast<std::size_t>(w) * (bpp / 8)) + 3) & ~std::size_t(3);
            if (data_off + row_stride * abs_h <= bytes.size()) {
                img.width = static_cast<std::uint32_t>(w);
                img.height = abs_h;
                img.rgba.resize(static_cast<std::size_t>(w) * abs_h * 4);
                for (std::uint32_t y = 0; y < abs_h; ++y) {
                    std::uint32_t src_y = flip ? (abs_h - 1 - y) : y;
                    const std::byte* row = bytes.data() + data_off + src_y * row_stride;
                    for (std::int32_t x = 0; x < w; ++x) {
                        std::size_t dst_idx = (static_cast<std::size_t>(y) * w + x) * 4;
                        std::size_t src_idx = static_cast<std::size_t>(x) * (bpp / 8);
                        img.rgba[dst_idx + 0] = static_cast<std::uint8_t>(row[src_idx + 2]);
                        img.rgba[dst_idx + 1] = static_cast<std::uint8_t>(row[src_idx + 1]);
                        img.rgba[dst_idx + 2] = static_cast<std::uint8_t>(row[src_idx + 0]);
                        img.rgba[dst_idx + 3] = (bpp == 32) ? static_cast<std::uint8_t>(row[src_idx + 3]) : 255;
                    }
                }
                return img;
            }
        }
    }
    return img;
}

[[nodiscard]] inline auto ocr_linux(std::span<const std::byte> img_bytes) -> std::string {
#if defined(__linux__)
    // Tier 1: Zero-compile-time-dependency dynamic dlopen of libtesseract C-API
    struct TessRuntime {
        void* handle = nullptr;
        void* (*create)() = nullptr;
        int (*init3)(void*, const char*, const char*) = nullptr;
        void (*set_image)(void*, const unsigned char*, int, int, int, int) = nullptr;
        char* (*get_utf8)(void*) = nullptr;
        void (*delete_text)(const char*) = nullptr;
        void (*destroy)(void*) = nullptr;
    };
    static const TessRuntime tess = [] {
        TessRuntime rt{};
        const char* libs[] = {"libtesseract.so.5", "libtesseract.so.4", "libtesseract.so"};
        for (const char* lib : libs) {
            rt.handle = ::dlopen(lib, RTLD_LAZY);
            if (rt.handle) break;
        }
        if (rt.handle) {
            rt.create = reinterpret_cast<decltype(rt.create)>(::dlsym(rt.handle, "TessBaseAPICreate"));
            rt.init3 = reinterpret_cast<decltype(rt.init3)>(::dlsym(rt.handle, "TessBaseAPIInit3"));
            rt.set_image = reinterpret_cast<decltype(rt.set_image)>(::dlsym(rt.handle, "TessBaseAPISetImage"));
            rt.get_utf8 = reinterpret_cast<decltype(rt.get_utf8)>(::dlsym(rt.handle, "TessBaseAPIGetUTF8Text"));
            rt.delete_text = reinterpret_cast<decltype(rt.delete_text)>(::dlsym(rt.handle, "TessDeleteText"));
            rt.destroy = reinterpret_cast<decltype(rt.destroy)>(::dlsym(rt.handle, "TessBaseAPIDelete"));
        }
        return rt;
    }();

    if (tess.create && tess.init3 && tess.set_image && tess.get_utf8 && tess.delete_text && tess.destroy) {
        auto decoded = decode_image_pixels(img_bytes);
        if (decoded.width > 0 && decoded.height > 0) {
            void* api = tess.create();
            if (api && tess.init3(api, nullptr, "eng") == 0) {
                tess.set_image(api, decoded.rgba.data(), static_cast<int>(decoded.width),
                               static_cast<int>(decoded.height), 4, static_cast<int>(decoded.width * 4));
                char* raw_txt = tess.get_utf8(api);
                std::string res = raw_txt ? trim_ws(raw_txt) : "";
                if (raw_txt) tess.delete_text(raw_txt);
                tess.destroy(api);
                if (!res.empty()) return res;
            } else if (api) {
                tess.destroy(api);
            }
        }
    }

    // Tier 2: Subprocess fallback to `tesseract` CLI if installed
    static int has_tesseract_cli = -1;
    if (has_tesseract_cli < 0) {
        has_tesseract_cli = (std::system("which tesseract >/dev/null 2>&1") == 0) ? 1 : 0;
    }
    if (has_tesseract_cli == 1) {
        static std::uint64_t seq = 0;
        std::string tmp_path = std::format("/tmp/cfire_ocr_{}_{}.img", static_cast<long>(::getpid()), ++seq);
        if (cfire::fs::write_bytes(tmp_path, img_bytes)) {
            std::string cmd = std::format("tesseract '{}' stdout --oem 1 -l eng 2>/dev/null", tmp_path);
            std::string out;
            if (FILE* pipe = ::popen(cmd.c_str(), "r")) {
                char buf[4096];
                while (std::size_t n = std::fread(buf, 1, sizeof(buf), pipe)) out.append(buf, n);
                ::pclose(pipe);
            }
            (void)cfire::fs::remove(tmp_path);
            out = trim_ws(out);
            if (!out.empty()) return out;
        }
    }
#else
    (void)img_bytes;
#endif
    // Tier 3: Optional local Vision LLM OCR fallback when CFIRE_OCR_MODEL is set
    std::string ocr_model = cfire::os::getenv("CFIRE_OCR_MODEL", "");
    if (!ocr_model.empty()) {
        std::string b64 = cfire::base64::encode(img_bytes);
        std::string body = std::format(
            R"({{"model":"{}","messages":[{{"role":"user","content":[{{"type":"text","text":"Extract all visible text from this image verbatim."}},{{"type":"image_url","image_url":{{"url":"data:image/png;base64,{}"}}}}]}}]}})",
            cfire::json::escape_string(ocr_model), b64);
        if (auto res = cfire::http::post("http://127.0.0.1:11434/v1/chat/completions", body, "application/json");
            res && res->status_code == 200) {
            if (auto dyn = cfire::json::parse_dynamic(res->body); dyn && dyn->is_object()) {
                if (auto ch = dyn->get("choices"); ch && ch->is_array() && !ch->as_array().empty()) {
                    if (auto msg = ch->as_array()[0].get("message"); msg && msg->is_object()) {
                        if (auto cnt = msg->get("content"); cnt && cnt->is_string()) {
                            return trim_ws(cnt->as_str());
                        }
                    }
                }
            }
        }
    }
    return "";
}

[[nodiscard]] inline auto embed_image_local(std::span<const std::byte> bytes, std::size_t dims = 384) -> std::vector<float> {
    std::vector<float> vec(dims, 0.0f);
    if (bytes.empty()) return vec;

    // 1. Pixel-true spatial color moments, Sobel HOG edge orientation histograms & 8x8 dHash
    auto decoded = decode_image_pixels(bytes);
    if (decoded.width > 0 && decoded.height > 0) {
        const std::uint32_t W = decoded.width;
        const std::uint32_t H = decoded.height;
        auto luma_at = [&](std::uint32_t x, std::uint32_t y) -> float {
            std::size_t idx = (static_cast<std::size_t>(y) * W + x) * 4;
            return 0.299f * decoded.rgba[idx] + 0.587f * decoded.rgba[idx + 1] + 0.114f * decoded.rgba[idx + 2];
        };

        // 1a. 4x4 spatial grid color & luminance moments
        for (std::uint32_t gy = 0; gy < 4; ++gy) {
            for (std::uint32_t gx = 0; gx < 4; ++gx) {
                std::uint32_t x0 = (gx * W) / 4;
                std::uint32_t x1 = std::max(x0 + 1, ((gx + 1) * W) / 4);
                std::uint32_t y0 = (gy * H) / 4;
                std::uint32_t y1 = std::max(y0 + 1, ((gy + 1) * H) / 4);
                double sum_r = 0, sum_g = 0, sum_b = 0, sum_l = 0;
                std::size_t count = 0;
                for (std::uint32_t y = y0; y < y1 && y < H; ++y) {
                    for (std::uint32_t x = x0; x < x1 && x < W; ++x) {
                        std::size_t idx = (static_cast<std::size_t>(y) * W + x) * 4;
                        sum_r += decoded.rgba[idx];
                        sum_g += decoded.rgba[idx + 1];
                        sum_b += decoded.rgba[idx + 2];
                        sum_l += luma_at(x, y);
                        ++count;
                    }
                }
                if (count > 0) {
                    int qr = static_cast<int>((sum_r / count) / 32.0);
                    int qg = static_cast<int>((sum_g / count) / 32.0);
                    int qb = static_cast<int>((sum_b / count) / 32.0);
                    int ql = static_cast<int>((sum_l / count) / 32.0);
                    accumulate_feature(vec, std::format("cell:{},{}:rgb:{},{},{}", gx, gy, qr, qg, qb), 3.0f);
                    accumulate_feature(vec, std::format("cell:{},{}:luma:{}", gx, gy, ql), 2.5f);
                }
            }
        }

        // 1b. 2x2 spatial grid x 8-bin Sobel edge orientation histogram (HOG)
        if (W >= 3 && H >= 3) {
            for (std::uint32_t qy = 0; qy < 2; ++qy) {
                for (std::uint32_t qx = 0; qx < 2; ++qx) {
                    float bins[8] = {0};
                    std::uint32_t x0 = std::max(1u, (qx * W) / 2);
                    std::uint32_t x1 = std::min(W - 1, ((qx + 1) * W) / 2);
                    std::uint32_t y0 = std::max(1u, (qy * H) / 2);
                    std::uint32_t y1 = std::min(H - 1, ((qy + 1) * H) / 2);
                    for (std::uint32_t y = y0; y < y1; ++y) {
                        for (std::uint32_t x = x0; x < x1; ++x) {
                            float gx_val = (luma_at(x + 1, y - 1) + 2.0f * luma_at(x + 1, y) + luma_at(x + 1, y + 1)) -
                                           (luma_at(x - 1, y - 1) + 2.0f * luma_at(x - 1, y) + luma_at(x - 1, y + 1));
                            float gy_val = (luma_at(x - 1, y + 1) + 2.0f * luma_at(x, y + 1) + luma_at(x + 1, y + 1)) -
                                           (luma_at(x - 1, y - 1) + 2.0f * luma_at(x, y - 1) + luma_at(x + 1, y - 1));
                            float mag = std::sqrt(gx_val * gx_val + gy_val * gy_val);
                            if (mag > 16.0f) {
                                float angle = std::atan2(gy_val, gx_val) + 3.14159265f; // [0, 2pi]
                                int bin = static_cast<int>((angle / (2.0f * 3.14159265f)) * 8.0f) & 7;
                                bins[bin] += mag;
                            }
                        }
                    }
                    int dom_bin = 0;
                    float max_mag = 0.0f;
                    for (int b = 0; b < 8; ++b) {
                        if (bins[b] > max_mag) { max_mag = bins[b]; dom_bin = b; }
                    }
                    if (max_mag > 0.0f) {
                        accumulate_feature(vec, std::format("hog:{},{}:dom:{}", qx, qy, dom_bin), 4.0f);
                    }
                }
            }
        }

        // 1c. 8x8 perceptual dHash horizontal gradient bits
        for (std::uint32_t dy = 0; dy < 8; ++dy) {
            for (std::uint32_t dx = 0; dx < 8; ++dx) {
                std::uint32_t sx0 = (dx * W) / 9;
                std::uint32_t sx1 = std::min(W - 1, ((dx + 1) * W) / 9);
                std::uint32_t sy = std::min(H - 1, (dy * H) / 8);
                int bit = (luma_at(sx0, sy) < luma_at(sx1, sy)) ? 1 : 0;
                accumulate_feature(vec, std::format("dhash:{},{}:{}", dx, dy, bit), 1.2f);
            }
        }
    }

    // 2. Structural header & multi-scale byte shingle projections (fallback for undecoded formats like WebP/JPEG)
    if (bytes.size() >= 24 && static_cast<std::uint8_t>(bytes[0]) == 0x89 && static_cast<std::uint8_t>(bytes[1]) == 'P') {
        std::uint32_t w = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[16])) << 24) |
                          (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[17])) << 16) |
                          (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[18])) << 8) |
                          static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[19]));
        std::uint32_t h = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[20])) << 24) |
                          (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[21])) << 16) |
                          (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[22])) << 8) |
                          static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[23]));
        accumulate_feature(vec, std::format("png:{}x{}", w, h), 2.0f);
    }

    if (decoded.width == 0) {
        std::size_t n = bytes.size();
        std::size_t step = std::max<std::size_t>(1, n / 512);
        for (std::size_t i = 0; i + 4 <= n; i += step) {
            std::string_view shingle(reinterpret_cast<const char*>(bytes.data() + i), 4);
            accumulate_feature(vec, shingle, 1.0f);
            std::uint8_t qbucket = static_cast<std::uint8_t>(bytes[i]) >> 4;
            std::size_t region = (i * 8) / n;
            char region_tok[4] = {'R', static_cast<char>('0' + region), static_cast<char>('A' + qbucket), '\0'};
            accumulate_feature(vec, std::string_view(region_tok, 3), 0.75f);
        }
    }

    // 3. Fuse any OCR or embedded text semantics into the same vector space
    std::string ocr_txt = ocr_apple_vision(bytes);
    if (ocr_txt.empty()) ocr_txt = ocr_linux(bytes);
    std::string meta_txt = extract_image_text_metadata(bytes);
    std::string combined_txt = ocr_txt;
    if (!meta_txt.empty()) {
        if (!combined_txt.empty()) combined_txt += "\n";
        combined_txt += meta_txt;
    }
    if (!combined_txt.empty()) {
        auto txt_vec = embed_trigram_hash(combined_txt, dims);
        for (std::size_t i = 0; i < dims && i < txt_vec.size(); ++i) {
            vec[i] += txt_vec[i] * 2.0f;
        }
    }

    return normalize(vec);
}

} // namespace detail

// --- Public Multimodal, Document Ingestion, OCR & ZIP Helpers (`core:ai`) ---

[[nodiscard]] inline auto create_zip(const std::vector<ZipEntry>& entries) -> std::vector<std::byte> {
    std::vector<std::byte> out;
    struct CdRecord {
        std::string name;
        std::uint32_t crc;
        std::uint32_t size;
        std::uint32_t offset;
    };
    std::vector<CdRecord> cd_records;

    for (const auto& e : entries) {
        std::uint32_t offset = static_cast<std::uint32_t>(out.size());
        std::uint32_t crc = detail::crc32_bytes(e.content);
        std::uint32_t sz = static_cast<std::uint32_t>(e.content.size());

        detail::append_u32_le(out, 0x04034b50u); // Local file header signature
        detail::append_u16_le(out, 20);          // Version needed
        detail::append_u16_le(out, 0);           // Flags
        detail::append_u16_le(out, 0);           // Method 0 = STORE
        detail::append_u16_le(out, 0);           // Mod time
        detail::append_u16_le(out, 0);           // Mod date
        detail::append_u32_le(out, crc);
        detail::append_u32_le(out, sz);
        detail::append_u32_le(out, sz);
        detail::append_u16_le(out, static_cast<std::uint16_t>(e.name.size()));
        detail::append_u16_le(out, 0);           // Extra len
        for (char c : e.name) out.push_back(static_cast<std::byte>(c));
        for (char c : e.content) out.push_back(static_cast<std::byte>(c));

        cd_records.push_back(CdRecord{e.name, crc, sz, offset});
    }

    std::uint32_t cd_start = static_cast<std::uint32_t>(out.size());
    for (const auto& r : cd_records) {
        detail::append_u32_le(out, 0x02014b50u); // Central directory file header
        detail::append_u16_le(out, 20);
        detail::append_u16_le(out, 20);
        detail::append_u16_le(out, 0);
        detail::append_u16_le(out, 0);
        detail::append_u16_le(out, 0);
        detail::append_u16_le(out, 0);
        detail::append_u32_le(out, r.crc);
        detail::append_u32_le(out, r.size);
        detail::append_u32_le(out, r.size);
        detail::append_u16_le(out, static_cast<std::uint16_t>(r.name.size()));
        detail::append_u16_le(out, 0);
        detail::append_u16_le(out, 0);
        detail::append_u16_le(out, 0);
        detail::append_u16_le(out, 0);
        detail::append_u32_le(out, 0);
        detail::append_u32_le(out, r.offset);
        for (char c : r.name) out.push_back(static_cast<std::byte>(c));
    }
    std::uint32_t cd_size = static_cast<std::uint32_t>(out.size()) - cd_start;

    detail::append_u32_le(out, 0x06054b50u); // End of central directory
    detail::append_u16_le(out, 0);
    detail::append_u16_le(out, 0);
    detail::append_u16_le(out, static_cast<std::uint16_t>(cd_records.size()));
    detail::append_u16_le(out, static_cast<std::uint16_t>(cd_records.size()));
    detail::append_u32_le(out, cd_size);
    detail::append_u32_le(out, cd_start);
    detail::append_u16_le(out, 0);
    return out;
}

[[nodiscard]] inline auto detect_mime_type(std::string_view path_or_name, std::span<const std::byte> bytes = {}) -> std::string {
    if (bytes.size() >= 5 && std::memcmp(bytes.data(), "%PDF-", 5) == 0) return "application/pdf";
    if (bytes.size() >= 8 && static_cast<std::uint8_t>(bytes[0]) == 0x89 && std::memcmp(bytes.data() + 1, "PNG", 3) == 0) return "image/png";
    if (bytes.size() >= 3 && static_cast<std::uint8_t>(bytes[0]) == 0xFF && static_cast<std::uint8_t>(bytes[1]) == 0xD8 && static_cast<std::uint8_t>(bytes[2]) == 0xFF) return "image/jpeg";
    if (bytes.size() >= 6 && (std::memcmp(bytes.data(), "GIF87a", 6) == 0 || std::memcmp(bytes.data(), "GIF89a", 6) == 0)) return "image/gif";
    if (bytes.size() >= 12 && std::memcmp(bytes.data(), "RIFF", 4) == 0 && std::memcmp(bytes.data() + 8, "WEBP", 4) == 0) return "image/webp";
    if (bytes.size() >= 12 && std::memcmp(bytes.data(), "RIFF", 4) == 0 && std::memcmp(bytes.data() + 8, "WAVE", 4) == 0) return "audio/wav";
    if (bytes.size() >= 2 && static_cast<std::uint8_t>(bytes[0]) == 'B' && static_cast<std::uint8_t>(bytes[1]) == 'M') return "image/bmp";

    std::string lower;
    lower.reserve(path_or_name.size());
    for (unsigned char c : path_or_name) lower.push_back(static_cast<char>(std::tolower(c)));

    if (lower.ends_with(".pdf")) return "application/pdf";
    if (lower.ends_with(".docx")) return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
    if (lower.ends_with(".pptx")) return "application/vnd.openxmlformats-officedocument.presentationml.presentation";
    if (lower.ends_with(".xlsx")) return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
    if (lower.ends_with(".odt")) return "application/vnd.oasis.opendocument.text";
    if (lower.ends_with(".odp")) return "application/vnd.oasis.opendocument.presentation";
    if (lower.ends_with(".ods")) return "application/vnd.oasis.opendocument.spreadsheet";
    if (lower.ends_with(".epub")) return "application/epub+zip";
    if (lower.ends_with(".png")) return "image/png";
    if (lower.ends_with(".bmp")) return "image/bmp";
    if (lower.ends_with(".jpg") || lower.ends_with(".jpeg")) return "image/jpeg";
    if (lower.ends_with(".webp")) return "image/webp";
    if (lower.ends_with(".gif")) return "image/gif";
    if (lower.ends_with(".svg")) return "image/svg+xml";
    if (lower.ends_with(".wav")) return "audio/wav";
    if (lower.ends_with(".mp3")) return "audio/mpeg";
    if (lower.ends_with(".html") || lower.ends_with(".htm")) return "text/html";
    if (lower.ends_with(".md") || lower.ends_with(".markdown")) return "text/markdown";
    if (lower.ends_with(".csv")) return "text/csv";
    if (lower.ends_with(".json")) return "application/json";

    if (bytes.size() >= 4 && detail::read_u32_le(bytes.data()) == 0x04034b50u) {
        auto entries = detail::extract_zip_entries(bytes);
        if (entries.contains("mimetype")) {
            std::string mt = detail::trim_ws(entries.at("mimetype"));
            if (!mt.empty()) return mt;
        }
        if (entries.contains("word/document.xml")) return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
        if (entries.contains("ppt/presentation.xml") || entries.contains("ppt/slides/slide1.xml")) return "application/vnd.openxmlformats-officedocument.presentationml.presentation";
        if (entries.contains("xl/workbook.xml") || entries.contains("xl/worksheets/sheet1.xml")) return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
        if (entries.contains("META-INF/container.xml")) return "application/epub+zip";
        if (entries.contains("content.xml")) return "application/vnd.oasis.opendocument.text";
        return "application/zip";
    }
    return "text/plain";
}

[[nodiscard]] inline auto ocr_bytes(std::span<const std::byte> data) -> std::expected<std::string, cfire::Error> {
    if (data.empty()) return cfire::make_error("cannot run OCR on empty byte buffer");
    std::string vis = detail::ocr_apple_vision(data);
    if (vis.empty()) vis = detail::ocr_linux(data);
    std::string meta = detail::extract_image_text_metadata(data);
    if (!vis.empty() && !meta.empty() && vis.find(meta) == std::string::npos) {
        return vis + "\n" + meta;
    }
    if (!vis.empty()) return vis;
    return meta;
}

[[nodiscard]] inline auto ocr_bytes(std::span<const std::uint8_t> data) -> std::expected<std::string, cfire::Error> {
    return ocr_bytes(std::as_bytes(data));
}

[[nodiscard]] inline auto ocr(const std::filesystem::path& path) -> std::expected<std::string, cfire::Error> {
    auto bytes = cfire::fs::read_bytes(path);
    if (!bytes) return std::unexpected(bytes.error());
    return ocr_bytes(*bytes);
}

[[nodiscard]] inline auto parse_document(std::span<const std::byte> bytes, std::string_view filename = "document.txt")
    -> std::expected<Document, cfire::Error>
{
    Document doc;
    doc.path = std::string(filename);
    doc.mime_type = detect_mime_type(filename, bytes);

    if (doc.mime_type == "application/pdf") {
        detail::parse_pdf_portable(bytes, doc);
        return doc;
    }

    if (doc.mime_type.find("wordprocessingml") != std::string::npos ||
        doc.mime_type.find("presentationml") != std::string::npos ||
        doc.mime_type.find("spreadsheetml") != std::string::npos ||
        doc.mime_type.find("opendocument") != std::string::npos ||
        doc.mime_type == "application/epub+zip" ||
        doc.mime_type == "application/zip") {
        auto entries = detail::extract_zip_entries(bytes);
        if (entries.contains("word/document.xml")) {
            detail::parse_docx_xml(entries.at("word/document.xml"), doc);
            return doc;
        }
        if (doc.mime_type.find("presentationml") != std::string::npos || entries.contains("ppt/slides/slide1.xml")) {
            detail::parse_pptx_zip(entries, doc);
            return doc;
        }
        if (doc.mime_type.find("spreadsheetml") != std::string::npos || entries.contains("xl/worksheets/sheet1.xml")) {
            detail::parse_xlsx_zip(entries, doc);
            return doc;
        }
        if (doc.mime_type.find("opendocument") != std::string::npos || entries.contains("content.xml")) {
            detail::parse_odf_zip(entries, doc.mime_type, doc);
            return doc;
        }
        if (doc.mime_type == "application/epub+zip" || entries.contains("META-INF/container.xml")) {
            detail::parse_epub_zip(entries, doc);
            return doc;
        }
    }

    if (doc.mime_type.starts_with("image/")) {
        doc.format = "image";
        auto ocr_res = ocr_bytes(bytes);
        doc.text = ocr_res ? *ocr_res : "";
        doc.sections.push_back(DocumentSection{
            .index = 1,
            .title = std::filesystem::path(filename).filename().string(),
            .text = doc.text
        });
        return doc;
    }

    std::string_view raw_text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (doc.mime_type == "text/html") {
        detail::parse_html_document(raw_text, doc);
        return doc;
    }

    if (doc.mime_type == "text/csv") {
        doc.format = "csv";
        auto rows = cfire::csv::parse(raw_text);
        std::string md;
        for (std::size_t r = 0; r < rows.size(); ++r) {
            md += "| ";
            for (const auto& cell : rows[r]) md += cell + " | ";
            if (!rows[r].empty()) md.pop_back();
            md += "\n";
            if (r == 0 && !rows[r].empty()) {
                md += "|";
                for (std::size_t c = 0; c < rows[r].size(); ++c) md += " --- |";
                md += "\n";
            }
        }
        doc.text = detail::trim_ws(md);
        doc.sections.push_back(DocumentSection{.index = 1, .title = "CSV Table", .text = doc.text});
        return doc;
    }

    doc.format = (doc.mime_type == "text/markdown") ? "markdown" : "text";
    detail::parse_markdown_sections(raw_text, doc);
    return doc;
}

[[nodiscard]] inline auto parse_document(std::span<const std::uint8_t> bytes, std::string_view filename = "document.txt")
    -> std::expected<Document, cfire::Error>
{
    return parse_document(std::as_bytes(bytes), filename);
}

[[nodiscard]] inline auto parse_document(std::string_view content, std::string_view filename = "document.md")
    -> std::expected<Document, cfire::Error>
{
    return parse_document(std::as_bytes(std::span<const char>(content.data(), content.size())), filename);
}

[[nodiscard]] inline auto load_document(const std::filesystem::path& path)
    -> std::expected<Document, cfire::Error>
{
    auto bytes = cfire::fs::read_bytes(path);
    if (!bytes) return std::unexpected(bytes.error());
    auto doc = parse_document(*bytes, path.string());
    if (!doc) return std::unexpected(doc.error());
    doc->path = path.string();
    return doc;
}

[[nodiscard]] inline auto chunk_document(
    const Document& doc,
    std::int32_t max_chars = 512,
    std::int32_t overlap = 64
) -> std::vector<DocumentSection> {
    std::vector<DocumentSection> out;
    if (doc.sections.empty()) {
        for (auto& ch : chunk_text(doc.text, max_chars, overlap)) {
            out.push_back(DocumentSection{.index = 1, .title = doc.path, .text = std::move(ch)});
        }
        return out;
    }
    for (const auto& sec : doc.sections) {
        auto pieces = chunk_text(sec.text, max_chars, overlap);
        if (pieces.empty() && !sec.text.empty()) {
            out.push_back(sec);
        } else {
            for (auto& p : pieces) {
                out.push_back(DocumentSection{
                    .index = sec.index,
                    .title = sec.title,
                    .text = std::move(p)
                });
            }
        }
    }
    return out;
}

[[nodiscard]] inline auto make_attachment(
    std::string_view filename,
    std::span<const std::byte> data,
    std::string_view mime_type = ""
) -> Attachment {
    Attachment att;
    att.filename = std::filesystem::path(filename).filename().string();
    att.mime_type = mime_type.empty() ? detect_mime_type(filename, data) : std::string(mime_type);
    att.data.assign(data.begin(), data.end());
    if (!att.mime_type.starts_with("image/") && !att.mime_type.starts_with("audio/")) {
        if (auto doc = parse_document(data, filename); doc && !doc->text.empty()) {
            att.extracted_text = doc->text;
        }
    }
    return att;
}

[[nodiscard]] inline auto load_attachment(const std::filesystem::path& path)
    -> std::expected<Attachment, cfire::Error>
{
    auto bytes = cfire::fs::read_bytes(path);
    if (!bytes) return std::unexpected(bytes.error());
    return make_attachment(path.string(), *bytes);
}

[[nodiscard]] inline auto user_with_file(std::string content, const std::filesystem::path& path)
    -> std::expected<Message, cfire::Error>
{
    auto att = load_attachment(path);
    if (!att) return std::unexpected(att.error());
    return user_with_attachment(std::move(content), std::move(*att));
}

[[nodiscard]] inline auto embed_image_bytes(
    std::string_view model,
    std::span<const std::byte> bytes,
    std::string_view mime_type = "image/png"
) -> std::expected<std::vector<float>, cfire::Error> {
    if (bytes.empty()) return cfire::make_error("cannot embed empty image buffer");
    if (model.empty() || model == "local" || model == "apple" || model == "native") {
        return detail::embed_image_local(bytes, 384);
    }
    // Cloud multimodal embedding fallback (e.g. Gemini embedContent with inlineData)
    std::string key = cfire::os::getenv("GEMINI_API_KEY", "");
    std::string url = std::format("https://generativelanguage.googleapis.com/v1beta/models/{}:embedContent", model);
    std::unordered_map<std::string, std::string> headers;
    headers["x-goog-api-key"] = key;
    std::string b64 = cfire::base64::encode(bytes);
    std::string body = std::format(
        R"({{"model":"models/{}","content":{{"parts":[{{"inlineData":{{"mimeType":"{}","data":"{}"}}}}]}}}})",
        cfire::json::escape_string(model),
        cfire::json::escape_string(mime_type),
        b64);
    auto res = cfire::http::post(url, body, "application/json", headers);
    if (!res) return std::unexpected(res.error());
    if (res->status_code >= 400) {
        return cfire::make_error(std::format("AI embed_image HTTP {} error: {}", res->status_code, res->body));
    }
    auto dyn = cfire::json::parse_dynamic(res->body);
    if (dyn && dyn->is_object()) {
        if (auto emb_obj = dyn->get("embedding"); emb_obj && emb_obj->is_object()) {
            if (auto vals = emb_obj->get("values"); vals && vals->is_array()) {
                std::vector<float> vec;
                vec.reserve(vals->as_array().size());
                for (const auto& v : vals->as_array()) vec.push_back(static_cast<float>(v.as_float()));
                return vec;
            }
        }
    }
    return cfire::make_error("image embedding vector not found in provider response");
}

[[nodiscard]] inline auto embed_image(
    std::string_view model,
    const std::filesystem::path& path
) -> std::expected<std::vector<float>, cfire::Error> {
    auto bytes = cfire::fs::read_bytes(path);
    if (!bytes) return std::unexpected(bytes.error());
    std::string mime = detect_mime_type(path.string(), *bytes);
    return embed_image_bytes(model, *bytes, mime);
}

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

    [[nodiscard]] auto ask_with_file(
        std::string_view model,
        std::string_view prompt,
        const std::filesystem::path& file_path,
        std::string_view system_prompt = ""
    ) const -> std::expected<std::string, cfire::Error> {
        auto m = user_with_file(std::string(prompt), file_path);
        if (!m) return std::unexpected(m.error());
        std::vector<Message> msgs = { std::move(*m) };
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

    template <typename T>
    [[nodiscard]] auto generate_struct_from_file(
        std::string_view model,
        std::string_view prompt,
        const std::filesystem::path& file_path,
        std::string_view system_prompt = ""
    ) const -> std::expected<T, cfire::Error> {
        auto m = user_with_file(std::string(prompt), file_path);
        if (!m) return std::unexpected(m.error());
        std::string schema_str = cfire::json::schema<T>();
        std::vector<Message> msgs = { std::move(*m) };
        auto res = chat_internal(model, msgs, {}, system_prompt, schema_str);
        if (!res) return std::unexpected(res.error());
        std::string clean_json = detail::strip_markdown_code_fence(res->text);
        if (clean_json.empty() && !res->tool_calls.empty()) {
            clean_json = res->tool_calls.front().arguments_json;
        }
        return cfire::json::parse<T>(clean_json);
    }

    [[nodiscard]] auto embed_image(
        std::string_view model,
        const std::filesystem::path& path
    ) const -> std::expected<std::vector<float>, cfire::Error> {
        return cfire::ai::embed_image(model, path);
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
#if defined(__EMSCRIPTEN__) || defined(PLATFORM_WEB)
            headers["anthropic-dangerous-direct-browser-access"] = "true";
#endif
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
#if defined(__EMSCRIPTEN__) || defined(PLATFORM_WEB)
            headers["anthropic-dangerous-direct-browser-access"] = "true";
#endif

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
                if (m.attachments.empty()) {
                    msgs_json += std::format(R"({{"role":"{}","content":"{}"}})",
                        cfire::json::escape_string(m.role),
                        cfire::json::escape_string(m.content));
                } else {
                    std::string blocks = "[";
                    bool first_b = true;
                    for (const auto& att : m.attachments) {
                        if (!first_b) blocks += ",";
                        first_b = false;
                        if (att.mime_type.starts_with("image/")) {
                            blocks += std::format(
                                R"({{"type":"image","source":{{"type":"base64","media_type":"{}","data":"{}"}}}})",
                                cfire::json::escape_string(att.mime_type),
                                cfire::base64::encode(att.data));
                        } else if (att.mime_type == "application/pdf") {
                            blocks += std::format(
                                R"({{"type":"document","source":{{"type":"base64","media_type":"application/pdf","data":"{}"}}}})",
                                cfire::base64::encode(att.data));
                        } else {
                            std::string doc_txt = std::format("[Attachment: {}]\n{}", att.filename, att.extracted_text);
                            blocks += std::format(R"({{"type":"text","text":"{}"}})", cfire::json::escape_string(doc_txt));
                        }
                    }
                    if (!m.content.empty()) {
                        if (!first_b) blocks += ",";
                        blocks += std::format(R"({{"type":"text","text":"{}"}})", cfire::json::escape_string(m.content));
                    }
                    blocks += "]";
                    msgs_json += std::format(R"({{"role":"{}","content":{}}})",
                        cfire::json::escape_string(m.role), blocks);
                }
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
                std::string parts = "[";
                bool first_p = true;
                for (const auto& att : m.attachments) {
                    if (!first_p) parts += ",";
                    first_p = false;
                    if (att.mime_type.starts_with("image/") || att.mime_type == "application/pdf" || att.mime_type.starts_with("audio/")) {
                        parts += std::format(
                            R"({{"inlineData":{{"mimeType":"{}","data":"{}"}}}})",
                            cfire::json::escape_string(att.mime_type),
                            cfire::base64::encode(att.data));
                    } else {
                        std::string doc_txt = std::format("[Attachment: {}]\n{}", att.filename, att.extracted_text);
                        parts += std::format(R"({{"text":"{}"}})", cfire::json::escape_string(doc_txt));
                    }
                }
                if (!m.content.empty() || m.attachments.empty()) {
                    if (!first_p) parts += ",";
                    parts += std::format(R"({{"text":"{}"}})", cfire::json::escape_string(m.content));
                }
                parts += "]";
                contents_json += std::format(R"({{"role":"{}","parts":{}}})", g_role, parts);
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
                if (m.attachments.empty()) {
                    msgs_json += std::format(R"({{"role":"{}","content":"{}"}})",
                        cfire::json::escape_string(m.role),
                        cfire::json::escape_string(m.content));
                } else {
                    std::string blocks = "[";
                    bool first_b = true;
                    if (!m.content.empty()) {
                        blocks += std::format(R"({{"type":"text","text":"{}"}})", cfire::json::escape_string(m.content));
                        first_b = false;
                    }
                    for (const auto& att : m.attachments) {
                        if (!first_b) blocks += ",";
                        first_b = false;
                        if (att.mime_type.starts_with("image/")) {
                            blocks += std::format(
                                R"({{"type":"image_url","image_url":{{"url":"data:{};base64,{}"}}}})",
                                cfire::json::escape_string(att.mime_type),
                                cfire::base64::encode(att.data));
                        } else if (!att.extracted_text.empty()) {
                            std::string doc_txt = std::format("[Attachment: {}]\n{}", att.filename, att.extracted_text);
                            blocks += std::format(R"({{"type":"text","text":"{}"}})", cfire::json::escape_string(doc_txt));
                        } else {
                            blocks += std::format(
                                R"({{"type":"file","file":{{"filename":"{}","file_data":"data:{};base64,{}"}}}})",
                                cfire::json::escape_string(att.filename),
                                cfire::json::escape_string(att.mime_type),
                                cfire::base64::encode(att.data));
                        }
                    }
                    blocks += "]";
                    msgs_json += std::format(R"({{"role":"{}","content":{}}})",
                        cfire::json::escape_string(m.role), blocks);
                }
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
        std::string local_override = cfire::os::getenv("CFIRE_LOCAL_EMBED_MODEL", "");
        if (!local_override.empty() && model == "local") {
            if (auto res = ollama().embed(local_override, text)) return res;
        }
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
