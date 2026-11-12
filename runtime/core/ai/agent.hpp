#pragma once
// cfire runtime — runtime/core/ai/agent.hpp
// Part of the ext:ai module implementation: unified tool registry for McpServer and Agent,
// multi-turn tool execution loops, and self-correcting structured output extraction.

#include "client.hpp"
#include <iostream>
#include <memory>

namespace cfire::ai {

// ---------------------------------------------------------------------------
// 5. MCP Server Helper (JSON-RPC 2.0 over stdio or HTTP)
// ---------------------------------------------------------------------------

class McpServer {
public:
    McpServer() = default;
    explicit McpServer(std::string name, std::string version = "1.0.0")
        : name_(std::move(name)), version_(std::move(version)) {}

    auto add_tool(
        std::string name,
        std::string description,
        std::string schema_json,
        std::function<std::expected<std::string, cfire::Error>(std::string_view)> handler
    ) -> void {
        tools_.push_back(RegisteredTool{
            Tool{std::move(name), std::move(description), std::move(schema_json)},
            std::move(handler)
        });
    }

    template <typename ArgsStruct, typename Fn>
    auto register_tool(std::string name, std::string description, Fn&& fn) -> void {
        std::string schema = cfire::json::schema<ArgsStruct>();
        add_tool(
            std::move(name),
            std::move(description),
            std::move(schema),
            [func = std::forward<Fn>(fn)](std::string_view args_json) -> std::expected<std::string, cfire::Error> {
                std::string payload = args_json.empty() ? "{}" : std::string(args_json);
                auto parsed = cfire::json::parse<ArgsStruct>(payload);
                if (!parsed) return std::unexpected(parsed.error());
                return detail::serialize_tool_return(func(*parsed));
            }
        );
    }

    template <typename Fn>
    auto register_tool_action(std::string name, std::string description, Fn&& fn) -> void {
        add_tool(
            std::move(name),
            std::move(description),
            R"({"type":"object","properties":{},"additionalProperties":false})",
            [func = std::forward<Fn>(fn)](std::string_view) -> std::expected<std::string, cfire::Error> {
                return detail::serialize_tool_return(func());
            }
        );
    }

    [[nodiscard]] auto tools() const noexcept -> const std::vector<RegisteredTool>& {
        return tools_;
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
            return "";
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
                    // Exception barrier (CF-140): a throwing tool reports isError instead of killing the server.
                    std::expected<std::string, cfire::Error> res;
                    try {
                        res = t.handler(args_str);
                    } catch (const std::exception& e) {
                        res = std::unexpected(cfire::Error(std::format("internal error in tool '{}': {}", t.spec.name, e.what())));
                    } catch (...) {
                        res = std::unexpected(cfire::Error(std::format("internal error in tool '{}': unknown exception", t.spec.name)));
                    }
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
    std::string name_{"cfire-mcp"};
    std::string version_{"1.0.0"};
    std::vector<RegisteredTool> tools_;
};

[[nodiscard]] inline auto make_mcp_server(std::string name, std::string version = "1.0.0") -> McpServer {
    return McpServer(std::move(name), std::move(version));
}

// ---------------------------------------------------------------------------
// 6. Agent Orchestrator (Multi-Turn Tool Loop + Self-Correcting Validation)
// ---------------------------------------------------------------------------

class Agent {
public:
    Agent() = default;
    explicit Agent(Client client)
        : model_(client.model()),
          client_(std::move(client)) {}
    Agent(Client client, std::string model, std::string system_prompt = "")
        : model_(model.empty() ? client.model() : std::move(model)),
          client_(std::move(client)),
          system_prompt_(std::move(system_prompt)) {}

    auto set_max_steps(std::int32_t steps) noexcept -> void {
        max_steps_ = steps > 0 ? steps : 1;
    }

    auto set_max_retries(std::int32_t retries) noexcept -> void {
        max_retries_ = retries >= 0 ? retries : 0;
    }

    [[nodiscard]] auto max_steps() const noexcept -> std::int64_t {
        return max_steps_;
    }

    [[nodiscard]] auto max_retries() const noexcept -> std::int64_t {
        return max_retries_;
    }

    auto add_tool(
        std::string name,
        std::string description,
        std::string schema_json,
        std::function<std::expected<std::string, cfire::Error>(std::string_view)> handler
    ) -> void {
        tools_.push_back(RegisteredTool{
            Tool{std::move(name), std::move(description), std::move(schema_json)},
            std::move(handler)
        });
    }

    template <typename ArgsStruct, typename Fn>
    auto register_tool(std::string name, std::string description, Fn&& fn) -> void {
        std::string schema = cfire::json::schema<ArgsStruct>();
        add_tool(
            std::move(name),
            std::move(description),
            std::move(schema),
            [func = std::forward<Fn>(fn)](std::string_view args_json) -> std::expected<std::string, cfire::Error> {
                auto parsed = cfire::json::parse<ArgsStruct>(args_json);
                if (!parsed) return std::unexpected(parsed.error());
                return detail::serialize_tool_return(func(*parsed));
            }
        );
    }

    template <typename Fn>
    auto register_tool_action(std::string name, std::string description, Fn&& fn) -> void {
        add_tool(
            std::move(name),
            std::move(description),
            R"({"type":"object","properties":{},"additionalProperties":false})",
            [func = std::forward<Fn>(fn)](std::string_view) -> std::expected<std::string, cfire::Error> {
                return detail::serialize_tool_return(func());
            }
        );
    }

    auto use_mcp(const McpServer& mcp) -> void {
        for (const auto& rt : mcp.tools()) {
            tools_.push_back(rt);
        }
    }

    [[nodiscard]] auto history() const noexcept -> const std::vector<Message>& {
        return history_;
    }

    auto clear_history() noexcept -> void {
        history_.clear();
    }

    [[nodiscard]] auto run_messages(const std::vector<Message>& messages) -> std::expected<ChatResponse, cfire::Error> {
        for (const auto& m : messages) {
            history_.push_back(m);
        }

        std::vector<Tool> tool_specs;
        tool_specs.reserve(tools_.size());
        for (const auto& rt : tools_) {
            tool_specs.push_back(rt.spec);
        }

        std::int32_t tool_error_retries = 0;
        for (std::int32_t step = 0; step < max_steps_; ++step) {
            auto res = client_.chat_with_tools(model_, history_, tool_specs, system_prompt_);
            if (!res) return std::unexpected(res.error());

            if (res->tool_calls.empty()) {
                history_.push_back(assistant(res->text));
                return *res;
            }

            history_.push_back(assistant_tool_calls(res->tool_calls, res->text));

            for (const auto& tc : res->tool_calls) {
                const RegisteredTool* matched = nullptr;
                for (const auto& rt : tools_) {
                    if (rt.spec.name == tc.name) {
                        matched = &rt;
                        break;
                    }
                }
                if (!matched) {
                    ++tool_error_retries;
                    std::string err_msg = std::format("Unknown tool '{}' requested; please call a registered tool.", tc.name);
                    if (tool_error_retries > max_retries_) {
                        return std::unexpected(cfire::Error("ai.Agent: " + err_msg));
                    }
                    history_.push_back(tool_result(tc.id, tc.name, err_msg, true));
                    continue;
                }

                std::string args_str = tc.arguments_json.empty() ? "{}" : tc.arguments_json;
                auto tool_out = matched->handler(args_str);
                if (!tool_out) {
                    ++tool_error_retries;
                    std::string err_msg = std::format(
                        "Tool '{}' failed validation or execution: {}. Fix arguments and retry.",
                        tc.name,
                        tool_out.error().message()
                    );
                    if (tool_error_retries > max_retries_) {
                        return std::unexpected(cfire::Error("ai.Agent: " + err_msg));
                    }
                    history_.push_back(tool_result(tc.id, tc.name, err_msg, true));
                } else {
                    history_.push_back(tool_result(tc.id, tc.name, *tool_out, false));
                }
            }
        }

        return std::unexpected(cfire::Error(std::format(
            "ai.Agent: exceeded max_steps ({}) without reaching a final response",
            max_steps_
        )));
    }

    [[nodiscard]] auto run(std::string_view prompt) -> std::expected<std::string, cfire::Error> {
        auto res = run_messages({user(std::string(prompt))});
        if (!res) return std::unexpected(res.error());
        return res->text;
    }

    template <typename Out, typename Validator>
    [[nodiscard]] auto run_struct_validated(std::string_view prompt, Validator&& validator) -> std::expected<Out, cfire::Error> {
        std::string schema_str = cfire::json::schema<Out>();
        std::string full_prompt = std::format(
            "{}\n\nRespond ONLY with a valid JSON object matching this JSON Schema:\n{}",
            prompt,
            schema_str
        );

        auto res = run_messages({user(std::move(full_prompt))});
        if (!res) return std::unexpected(res.error());

        for (std::int32_t attempt = 0; attempt <= max_retries_; ++attempt) {
            std::string clean_json = detail::strip_markdown_code_fence(res->text);
            auto parsed = cfire::json::parse<Out>(clean_json);
            if (!parsed) {
                if (attempt == max_retries_) {
                    return std::unexpected(parsed.error());
                }
                std::string retry_msg = std::format(
                    "Output validation failed: {}. Respond ONLY with valid JSON matching the schema.",
                    parsed.error().message()
                );
                res = run_messages({user(std::move(retry_msg))});
                if (!res) return std::unexpected(res.error());
                continue;
            }

            auto val_res = validator(*parsed);
            if (!val_res) {
                if (attempt == max_retries_) {
                    return std::unexpected(val_res.error());
                }
                std::string retry_msg = std::format(
                    "Output validation failed: {}. Respond ONLY with valid JSON matching the schema.",
                    val_res.error().message()
                );
                res = run_messages({user(std::move(retry_msg))});
                if (!res) return std::unexpected(res.error());
                continue;
            }

            return *parsed;
        }

        return std::unexpected(cfire::Error("ai.Agent: structured output validation failed"));
    }

    template <typename Out>
    [[nodiscard]] auto run_struct(std::string_view prompt) -> std::expected<Out, cfire::Error> {
        return run_struct_validated<Out>(prompt, [](const Out&) -> std::expected<void, cfire::Error> {
            return {};
        });
    }

private:
    std::string model_{"local"};
    Client client_;
    std::string system_prompt_;
    std::vector<RegisteredTool> tools_;
    std::vector<Message> history_;
    std::int32_t max_steps_{8};
    std::int32_t max_retries_{2};
};

[[nodiscard]] inline auto make_agent(Client client) -> Agent {
    return Agent(std::move(client));
}

[[nodiscard]] inline auto make_agent(Client client, std::string model, std::string system_prompt = "") -> Agent {
    return Agent(std::move(client), std::move(model), std::move(system_prompt));
}

} // namespace cfire::ai
