#pragma once
// runtime/core/ai/vectors.hpp — Core AI Types, SIMD Vector Similarity, Chunking & Local Feature Embeddings

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


} // namespace detail
} // namespace cfire::ai
