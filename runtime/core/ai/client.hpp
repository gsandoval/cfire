#pragma once
// runtime/core/ai/client.hpp — Unified Cloud/Local LLM Client & Model Context Protocol (MCP) Server

#include "vision.hpp"

namespace cfire::ai {

// --- Unified AI Client (`OpenAI`, `Anthropic`, `Gemini`, `Ollama`, `OpenAICompatible`, `Apple`) ---

class Client {
public:
    using MockHandler = std::function<std::expected<ChatResponse, cfire::Error>(
        const std::vector<Message>&,
        const std::vector<Tool>&
    )>;

    Provider provider = Provider::Local;
    std::string api_key{};
    std::string base_url{"local://on-device"};
    std::string default_model{"local"};
    std::shared_ptr<MockHandler> mock_handler_{};

    Client() = default;
    Client(Provider p, std::string key, std::string url, std::string model = "local")
        : provider(p), api_key(std::move(key)), base_url(std::move(url)), default_model(std::move(model)) {}
    explicit Client(MockHandler handler)
        : provider(Provider::Mock),
          api_key(""),
          base_url("mock://local"),
          default_model("mock"),
          mock_handler_(std::make_shared<MockHandler>(std::move(handler))) {}

    auto set_model(std::string_view m) -> void {
        default_model = std::string(m);
    }

    [[nodiscard]] auto model() const noexcept -> const std::string& {
        return default_model;
    }

    [[nodiscard]] auto ask(
        std::string_view prompt
    ) const -> std::expected<std::string, cfire::Error> {
        return ask(default_model, prompt, "");
    }

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
        std::string_view prompt,
        const std::filesystem::path& file_path
    ) const -> std::expected<std::string, cfire::Error> {
        return ask_with_file(default_model, prompt, file_path, "");
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
        const std::vector<Message>& messages
    ) const -> std::expected<ChatResponse, cfire::Error> {
        return chat_internal(default_model, messages, {}, "", "");
    }

    [[nodiscard]] auto chat(
        std::string_view model,
        const std::vector<Message>& messages,
        std::string_view system_prompt = ""
    ) const -> std::expected<ChatResponse, cfire::Error> {
        return chat_internal(model, messages, {}, system_prompt, "");
    }

    [[nodiscard]] auto chat_with_tools(
        const std::vector<Message>& messages,
        const std::vector<Tool>& tools
    ) const -> std::expected<ChatResponse, cfire::Error> {
        return chat_internal(default_model, messages, tools, "", "");
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
        std::string_view prompt
    ) const -> std::expected<T, cfire::Error> {
        return generate_struct<T>(default_model, prompt, "", 1);
    }

    template <typename T>
    [[nodiscard]] auto generate_struct(
        std::string_view model,
        std::string_view prompt,
        std::string_view system_prompt = "",
        std::int32_t max_retries = 1
    ) const -> std::expected<T, cfire::Error> {
        std::string schema_str = cfire::json::schema<T>();
        std::vector<Message> msgs = { Message{.role = "user", .content = std::string(prompt)} };
        std::int32_t limit = std::max<std::int32_t>(0, max_retries);
        for (std::int32_t attempt = 0; attempt <= limit; ++attempt) {
            auto res = chat_internal(model, msgs, {}, system_prompt, schema_str);
            if (!res) return std::unexpected(res.error());
            std::string clean_json = detail::strip_markdown_code_fence(res->text);
            if (clean_json.empty() && !res->tool_calls.empty()) {
                clean_json = res->tool_calls.front().arguments_json;
            }
            auto parsed = cfire::json::parse<T>(clean_json);
            if (parsed) return parsed;
            if (attempt < limit) {
                msgs.push_back(assistant(res->text.empty() ? clean_json : res->text));
                msgs.push_back(user(std::format(
                    "Validation failed: {}. Please respond with valid JSON matching the required schema.",
                    parsed.error().message())));
                continue;
            }
            return std::unexpected(parsed.error());
        }
        return cfire::make_error("generate_struct exhausted retries");
    }

    template <typename T>
    [[nodiscard]] auto generate_struct_from_file(
        std::string_view prompt,
        const std::filesystem::path& file_path
    ) const -> std::expected<T, cfire::Error> {
        return generate_struct_from_file<T>(default_model, prompt, file_path, "", 1);
    }

    template <typename T>
    [[nodiscard]] auto generate_struct_from_file(
        std::string_view model,
        std::string_view prompt,
        const std::filesystem::path& file_path,
        std::string_view system_prompt = "",
        std::int32_t max_retries = 1
    ) const -> std::expected<T, cfire::Error> {
        auto m = user_with_file(std::string(prompt), file_path);
        if (!m) return std::unexpected(m.error());
        std::string schema_str = cfire::json::schema<T>();
        std::vector<Message> msgs = { std::move(*m) };
        std::int32_t limit = std::max<std::int32_t>(0, max_retries);
        for (std::int32_t attempt = 0; attempt <= limit; ++attempt) {
            auto res = chat_internal(model, msgs, {}, system_prompt, schema_str);
            if (!res) return std::unexpected(res.error());
            std::string clean_json = detail::strip_markdown_code_fence(res->text);
            if (clean_json.empty() && !res->tool_calls.empty()) {
                clean_json = res->tool_calls.front().arguments_json;
            }
            auto parsed = cfire::json::parse<T>(clean_json);
            if (parsed) return parsed;
            if (attempt < limit) {
                msgs.push_back(assistant(res->text.empty() ? clean_json : res->text));
                msgs.push_back(user(std::format(
                    "Validation failed: {}. Please respond with valid JSON matching the required schema.",
                    parsed.error().message())));
                continue;
            }
            return std::unexpected(parsed.error());
        }
        return cfire::make_error("generate_struct_from_file exhausted retries");
    }

    [[nodiscard]] auto embed_image(
        const std::filesystem::path& path
    ) const -> std::expected<std::vector<float>, cfire::Error> {
        return embed_image(default_model, path);
    }

    [[nodiscard]] auto embed_image(
        std::string_view model,
        const std::filesystem::path& path
    ) const -> std::expected<std::vector<float>, cfire::Error> {
        return cfire::ai::embed_image(model, path);
    }

    [[nodiscard]] auto ocr(
        const std::filesystem::path& path
    ) const -> std::expected<std::string, cfire::Error> {
        if (provider == Provider::Local || api_key.empty()) {
            return cfire::ai::ocr(path);
        }
        if (auto cloud_res = ask_with_file(default_model, "Extract all text from this image verbatim.", path)) {
            return cloud_res;
        }
        return cfire::ai::ocr(path);
    }

    [[nodiscard]] auto detect_language(
        std::string_view text
    ) const -> std::string {
        return cfire::ai::detect_language(text);
    }

    template <typename OnToken>
    [[nodiscard]] auto stream(
        std::string_view prompt,
        OnToken&& on_token
    ) const -> std::expected<std::string, cfire::Error> {
        return stream(default_model, prompt, std::forward<OnToken>(on_token));
    }

    template <typename OnToken>
    [[nodiscard]] auto stream(
        const std::vector<Message>& messages,
        OnToken&& on_token
    ) const -> std::expected<std::string, cfire::Error> {
        return stream(default_model, messages, std::forward<OnToken>(on_token));
    }

    template <typename OnToken>
    [[nodiscard]] auto stream(
        std::string_view model,
        std::string_view prompt,
        OnToken&& on_token
    ) const -> std::expected<std::string, cfire::Error> {
        std::vector<Message> vec = { Message{.role = "user", .content = std::string(prompt)} };
        return stream(model, vec, std::forward<OnToken>(on_token));
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
        const std::vector<Message>& messages,
        OnToken&& on_token
    ) const -> std::expected<std::string, cfire::Error> {
        std::string last_prompt = messages.empty() ? "" : messages.back().content;
        if (provider == Provider::Mock && mock_handler_) {
            auto mres = (*mock_handler_)(messages, {});
            if (!mres) return std::unexpected(mres.error());
            on_token(mres->text);
            return mres->text;
        }
        if (provider == Provider::Apple) {
            std::string reply = std::format("[Apple On-Device AI ({})] {}", detect_language(last_prompt), last_prompt);
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
            auto [sys_str, msgs_json] = build_anthropic_messages(messages, "");
            body = std::format(R"({{"model":"{}","max_tokens":2048,"stream":true)", cfire::json::escape_string(model));
            if (!sys_str.empty()) {
                body += std::format(R"(,"system":"{}")", cfire::json::escape_string(sys_str));
            }
            body += std::format(R"(,"messages":{}}})", msgs_json);
        } else if (provider == Provider::Gemini) {
            url = std::format("{}/v1beta/models/{}:streamGenerateContent?alt=sse", base_url, model);
            headers["x-goog-api-key"] = api_key;
            auto [sys_str, contents_json] = build_gemini_contents(messages, "");
            body = std::format(R"({{"contents":{})", contents_json);
            if (!sys_str.empty()) {
                body += std::format(R"(,"systemInstruction":{{"parts":[{{"text":"{}"}}]}})",
                    cfire::json::escape_string(sys_str));
            }
            body += "}";
        } else {
            // OpenAI, Ollama (/v1/chat/completions), OpenAICompatible
            url = base_url + "/v1/chat/completions";
            if (!api_key.empty()) {
                headers["Authorization"] = "Bearer " + api_key;
            }
            std::string msgs_json = build_openai_messages(messages, "");
            body = std::format(
                R"({{"model":"{}","stream":true,"messages":{}}})",
                cfire::json::escape_string(model),
                msgs_json);
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
        std::string_view text
    ) const -> std::expected<std::vector<float>, cfire::Error> {
        return embed(default_model, text);
    }

    [[nodiscard]] auto embed(
        std::string_view model,
        std::string_view text
    ) const -> std::expected<std::vector<float>, cfire::Error> {
        if (model.empty() || model == "local" || model == "apple" || model == "native" || provider == Provider::Local) {
            std::string local_override = cfire::os::getenv("CFIRE_LOCAL_EMBED_MODEL", "");
            if (!local_override.empty() && (model.empty() || model == "local")) {
                Client oll(Provider::Ollama, "", "http://127.0.0.1:11434", local_override);
                if (auto res = oll.embed(local_override, text)) return res;
            }
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
    [[nodiscard]] static auto build_anthropic_messages(
        const std::vector<Message>& messages,
        std::string_view initial_system
    ) -> std::pair<std::string, std::string> {
        std::string sys_str(initial_system);
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
            if (m.role == "tool") {
                msgs_json += std::format(
                    R"({{"role":"user","content":[{{"type":"tool_result","tool_use_id":"{}","content":"{}","is_error":{}}}]}})",
                    cfire::json::escape_string(m.tool_call_id),
                    cfire::json::escape_string(m.content),
                    m.is_error ? "true" : "false");
            } else if (m.role == "assistant" && !m.tool_calls.empty()) {
                std::string blocks = "[";
                bool first_b = true;
                if (!m.content.empty()) {
                    blocks += std::format(R"({{"type":"text","text":"{}"}})", cfire::json::escape_string(m.content));
                    first_b = false;
                }
                for (const auto& tc : m.tool_calls) {
                    if (!first_b) blocks += ",";
                    first_b = false;
                    std::string args_obj = tc.arguments_json.empty() ? "{}" : tc.arguments_json;
                    blocks += std::format(
                        R"({{"type":"tool_use","id":"{}","name":"{}","input":{}}})",
                        cfire::json::escape_string(tc.id),
                        cfire::json::escape_string(tc.name),
                        args_obj);
                }
                blocks += "]";
                msgs_json += std::format(R"({{"role":"assistant","content":{}}})", blocks);
            } else if (m.attachments.empty()) {
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
        return {std::move(sys_str), std::move(msgs_json)};
    }

    [[nodiscard]] static auto build_gemini_contents(
        const std::vector<Message>& messages,
        std::string_view initial_system
    ) -> std::pair<std::string, std::string> {
        std::string sys_str(initial_system);
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
            if (m.role == "tool") {
                contents_json += std::format(
                    R"({{"role":"user","parts":[{{"functionResponse":{{"name":"{}","response":{{"result":"{}","is_error":{}}}}}}}]}})",
                    cfire::json::escape_string(m.tool_name),
                    cfire::json::escape_string(m.content),
                    m.is_error ? "true" : "false");
                continue;
            }
            if (m.role == "assistant" && !m.tool_calls.empty()) {
                std::string parts = "[";
                bool first_p = true;
                if (!m.content.empty()) {
                    parts += std::format(R"({{"text":"{}"}})", cfire::json::escape_string(m.content));
                    first_p = false;
                }
                for (const auto& tc : m.tool_calls) {
                    if (!first_p) parts += ",";
                    first_p = false;
                    std::string args_obj = tc.arguments_json.empty() ? "{}" : tc.arguments_json;
                    parts += std::format(
                        R"({{"functionCall":{{"name":"{}","args":{}}}}})",
                        cfire::json::escape_string(tc.name),
                        args_obj);
                }
                parts += "]";
                contents_json += std::format(R"({{"role":"model","parts":{}}})", parts);
                continue;
            }
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
        return {std::move(sys_str), std::move(contents_json)};
    }

    [[nodiscard]] static auto build_openai_messages(
        const std::vector<Message>& messages,
        std::string_view initial_system
    ) -> std::string {
        std::string msgs_json = "[";
        bool first_m = true;
        if (!initial_system.empty()) {
            msgs_json += std::format(R"({{"role":"system","content":"{}"}})",
                cfire::json::escape_string(initial_system));
            first_m = false;
        }
        for (const auto& m : messages) {
            if (!first_m) msgs_json += ",";
            first_m = false;
            if (m.role == "tool") {
                msgs_json += std::format(
                    R"({{"role":"tool","tool_call_id":"{}","content":"{}"}})",
                    cfire::json::escape_string(m.tool_call_id),
                    cfire::json::escape_string(m.content));
            } else if (m.role == "assistant" && !m.tool_calls.empty()) {
                std::string tc_arr = "[";
                bool first_t = true;
                for (const auto& tc : m.tool_calls) {
                    if (!first_t) tc_arr += ",";
                    first_t = false;
                    tc_arr += std::format(
                        R"({{"id":"{}","type":"function","function":{{"name":"{}","arguments":"{}"}}}})",
                        cfire::json::escape_string(tc.id),
                        cfire::json::escape_string(tc.name),
                        cfire::json::escape_string(tc.arguments_json));
                }
                tc_arr += "]";
                msgs_json += std::format(
                    R"({{"role":"assistant","content":"{}","tool_calls":{}}})",
                    cfire::json::escape_string(m.content),
                    tc_arr);
            } else if (m.attachments.empty()) {
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
        return msgs_json;
    }

    [[nodiscard]] auto chat_internal(
        std::string_view model,
        const std::vector<Message>& messages,
        const std::vector<Tool>& tools,
        std::string_view system_prompt,
        std::string_view response_schema_json
    ) const -> std::expected<ChatResponse, cfire::Error> {
        if (provider == Provider::Mock && mock_handler_) {
            return (*mock_handler_)(messages, tools);
        }
        if (provider == Provider::Apple) {
            std::string last_user;
            for (const auto& m : messages) {
                if (m.role == "user") last_user = m.content;
            }
            ChatResponse out;
            out.model = "apple-on-device";
            if (!response_schema_json.empty()) {
                std::string svc = "service";
                int sev = 1;
                if (auto p = last_user.find("Service="); p != std::string::npos) {
                    auto end_p = last_user.find_first_of(" \t\r\n,", p);
                    svc = last_user.substr(p + 8, (end_p == std::string::npos) ? std::string::npos : end_p - (p + 8));
                }
                if (auto p = last_user.find("severity "); p != std::string::npos) {
                    sev = std::atoi(last_user.c_str() + p + 9);
                } else if (auto p = last_user.find("severity="); p != std::string::npos) {
                    sev = std::atoi(last_user.c_str() + p + 9);
                }
                out.text = std::format(R"({{"service":"{}","severity":{}}})", svc, sev);
                return out;
            }
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
            auto [sys_str, msgs_json] = build_anthropic_messages(messages, system_prompt);
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

            auto [sys_str, contents_json] = build_gemini_contents(messages, system_prompt);
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
            std::string msgs_json = build_openai_messages(messages, system_prompt);
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

// --- Provider Factory Constructors (`ai.local`, `ai.apple`, `ai.openai`, `ai.anthropic`, `ai.gemini`, `ai.ollama`, `ai.mock`) ---

[[nodiscard]] inline auto local() -> Client {
    return Client(Provider::Local, "", "local://on-device", "local");
}

[[nodiscard]] inline auto apple() -> Client {
    return Client(Provider::Local, "", "apple://local", "local");
}

[[nodiscard]] inline auto openai(std::string_view api_key = "", std::string_view model = "gpt-4o-mini") -> Client {
    std::string key = api_key.empty() ? cfire::os::getenv("OPENAI_API_KEY", "") : std::string(api_key);
    return Client(Provider::OpenAI, std::move(key), "https://api.openai.com", std::string(model));
}

[[nodiscard]] inline auto anthropic(std::string_view api_key = "", std::string_view model = "claude-3-5-sonnet-latest") -> Client {
    std::string key = api_key.empty() ? cfire::os::getenv("ANTHROPIC_API_KEY", "") : std::string(api_key);
    return Client(Provider::Anthropic, std::move(key), "https://api.anthropic.com", std::string(model));
}

[[nodiscard]] inline auto gemini(std::string_view api_key = "", std::string_view model = "gemini-2.5-flash") -> Client {
    std::string key = api_key.empty() ? cfire::os::getenv("GEMINI_API_KEY", "") : std::string(api_key);
    return Client(Provider::Gemini, std::move(key), "https://generativelanguage.googleapis.com", std::string(model));
}

[[nodiscard]] inline auto ollama(std::string_view host = "http://127.0.0.1:11434", std::string_view model = "llama3.2") -> Client {
    std::string url(host);
    while (!url.empty() && url.back() == '/') url.pop_back();
    return Client(Provider::Ollama, "", std::move(url), std::string(model));
}

[[nodiscard]] inline auto openai_compatible(std::string_view base_url, std::string_view api_key = "", std::string_view model = "default") -> Client {
    std::string url(base_url);
    while (!url.empty() && url.back() == '/') url.pop_back();
    return Client(Provider::OpenAICompatible, std::string(api_key), std::move(url), std::string(model));
}

template <typename Fn>
[[nodiscard]] inline auto mock(Fn&& handler) -> Client {
    return Client([fn = std::forward<Fn>(handler)](
        const std::vector<Message>& messages,
        const std::vector<Tool>& tools
    ) mutable -> std::expected<ChatResponse, cfire::Error> {
        using Ret = std::invoke_result_t<decltype(fn)&, const std::vector<Message>&, const std::vector<Tool>&>;
        if constexpr (detail::is_std_expected<std::decay_t<Ret>>::value) {
            return fn(messages, tools);
        } else {
            return fn(messages, tools);
        }
    });
}

[[nodiscard]] inline auto mock_sequence(std::vector<ChatResponse> responses) -> Client {
    auto idx = std::make_shared<std::size_t>(0);
    auto seq = std::make_shared<std::vector<ChatResponse>>(std::move(responses));
    return Client([idx, seq](
        const std::vector<Message>&,
        const std::vector<Tool>&
    ) -> std::expected<ChatResponse, cfire::Error> {
        if (seq->empty()) {
            return cfire::make_error("ai.mock_sequence configured with empty response list");
        }
        std::size_t cur = (*idx < seq->size()) ? (*idx)++ : (seq->size() - 1);
        return (*seq)[cur];
    });
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

} // namespace cfire::ai
