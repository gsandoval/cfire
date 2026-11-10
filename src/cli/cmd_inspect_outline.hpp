#pragma once
// src/cli/cmd_inspect_outline.hpp — `cfire outline` & `cfire inspect` Subcommands

#include "scaffold.hpp"

namespace cfire::cli {

[[nodiscard]] inline auto run_outline_subcommand(int argc, char** argv) -> int {
        std::filesystem::path target_path = ".";
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg != "--json") {
                target_path = arg;
            }
        }
        std::vector<std::filesystem::path> cf_files;
        std::error_code ec;
        if (std::filesystem::is_directory(target_path, ec)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(target_path, ec)) {
                if (entry.is_regular_file() && entry.path().extension() == ".cf") {
                    cf_files.push_back(entry.path());
                }
            }
        } else if (std::filesystem::exists(target_path, ec)) {
            cf_files.push_back(target_path);
        } else {
            std::println(stderr, "target '{}' not found", target_path.string());
            return 1;
        }
        std::sort(cf_files.begin(), cf_files.end());

        std::string json = "{\n  \"files\": {\n";
        bool first_file = true;
        for (const auto& f : cf_files) {
            auto content = cfire::build::read_file_text(f);
            if (!content) continue;
            auto ast = cfire::frontend::parse_source(f.string(), *content);
            if (!ast) continue;

            if (!first_file) json += ",\n";
            first_file = false;

            json += std::format("    \"{}\": {{\n", f.string());
            json += std::format("      \"package\": \"{}\",\n", ast->package_name);

            json += "      \"imports\": [";
            for (std::size_t i = 0; i < ast->imports.size(); ++i) {
                if (i > 0) json += ", ";
                json += std::format("\"{}\"", ast->imports[i].path);
            }
            json += "],\n";

            json += "      \"types\": [\n";
            bool first_type = true;
            for (const auto& st : ast->structs) {
                if (!first_type) json += ",\n";
                first_type = false;
                json += std::format("        {{\"name\": \"{}\", \"kind\": \"struct\", \"fields\": [", st.name);
                for (std::size_t i = 0; i < st.fields.size(); ++i) {
                    if (i > 0) json += ", ";
                    json += std::format("{{\"name\": \"{}\", \"type\": \"{}\", \"tag\": \"{}\"}}",
                        st.fields[i].name,
                        cfire::lsp::detail::json_escape(cfire::fmt::format_type_ref(st.fields[i].type)),
                        cfire::lsp::detail::json_escape(st.fields[i].raw_tag));
                }
                json += "]}";
            }
            for (const auto& en : ast->enums) {
                if (!first_type) json += ",\n";
                first_type = false;
                json += std::format("        {{\"name\": \"{}\", \"kind\": \"enum\", \"variants\": [", en.name);
                for (std::size_t i = 0; i < en.variants.size(); ++i) {
                    if (i > 0) json += ", ";
                    std::string var_str = en.variants[i].name;
                    if (!en.variants[i].payload_types.empty()) {
                        var_str += "(";
                        for (std::size_t pt = 0; pt < en.variants[i].payload_types.size(); ++pt) {
                            if (pt > 0) var_str += ", ";
                            var_str += cfire::fmt::format_type_ref(en.variants[i].payload_types[pt]);
                        }
                        var_str += ")";
                    }
                    json += std::format("\"{}\"", cfire::lsp::detail::json_escape(var_str));
                }
                json += "]}";
            }
            for (const auto& ifc : ast->interfaces) {
                if (!first_type) json += ",\n";
                first_type = false;
                json += std::format("        {{\"name\": \"{}\", \"kind\": \"interface\", \"methods\": [", ifc.name);
                for (std::size_t i = 0; i < ifc.methods.size(); ++i) {
                    if (i > 0) json += ", ";
                    std::string sig = std::format("fn {}(", ifc.methods[i].name);
                    for (std::size_t p = 0; p < ifc.methods[i].param_types.size(); ++p) {
                        if (p > 0) sig += ", ";
                        sig += cfire::fmt::format_type_ref(ifc.methods[i].param_types[p]);
                    }
                    sig += std::format(") -> {}", cfire::fmt::format_type_ref(ifc.methods[i].return_type));
                    json += std::format("\"{}\"", cfire::lsp::detail::json_escape(sig));
                }
                json += "]}";
            }
            json += "\n      ],\n";
            json += "      \"global_vars\": [\n";
            for (std::size_t i = 0; i < ast->global_vars.size(); ++i) {
                if (i > 0) json += ",\n";
                const auto& gv = ast->global_vars[i];
                std::string ctor_call;
                std::vector<std::string> args;
                std::string type_arg;
                if (gv.value) {
                    if (const auto* c = std::get_if<cfire::frontend::CallExpr>(&gv.value->node)) {
                        if (const auto* m = std::get_if<cfire::frontend::MemberExpr>(&c->callee->node)) {
                            if (const auto* id = std::get_if<cfire::frontend::IdentExpr>(&m->object->node)) {
                                ctor_call = id->name + "." + m->field;
                            }
                        }
                        if (!c->type_args.empty()) {
                            type_arg = c->type_args[0].name;
                        }
                        for (const auto& a : c->args) {
                            if (const auto* lit = std::get_if<cfire::frontend::LiteralExpr>(&a.value->node)) {
                                std::string s = lit->value;
                                if (s.size() >= 2 && (s.front() == '"' || s.front() == '`')) s = s.substr(1, s.size() - 2);
                                args.push_back(s);
                            } else if (const auto* id = std::get_if<cfire::frontend::IdentExpr>(&a.value->node)) {
                                args.push_back(id->name);
                            }
                        }
                    }
                }
                json += std::format("        {{\"name\": \"{}\", \"line\": {}, \"call\": \"{}\", \"type_arg\": \"{}\", \"args\": [",
                    gv.name, gv.line, ctor_call, type_arg);
                for (std::size_t a = 0; a < args.size(); ++a) {
                    if (a > 0) json += ", ";
                    json += std::format("\"{}\"", cfire::lsp::detail::json_escape(args[a]));
                }
                json += "]}";
            }
            json += "\n      ],\n";
            json += "      \"functions\": [\n";
            for (std::size_t i = 0; i < ast->functions.size(); ++i) {
                if (i > 0) json += ",\n";
                const auto& fn = ast->functions[i];
                std::string rec_str;
                if (fn.receiver) {
                    std::string rec_intent = (fn.receiver->intent == "mut") ? "mut " : "";
                    rec_str = std::format("({}{}: {})", rec_intent, fn.receiver->name, cfire::fmt::format_type_ref(fn.receiver->type));
                }
                std::string tparams_str = "[";
                for (std::size_t tp = 0; tp < fn.type_params.size(); ++tp) {
                    if (tp > 0) tparams_str += ", ";
                    const auto& t = fn.type_params[tp];
                    tparams_str += std::format("\"{}\"", t.constraint.empty() ? t.name : std::format("{}: {}", t.name, t.constraint));
                }
                tparams_str += "]";

                json += std::format("        {{\"name\": \"{}\", \"receiver\": \"{}\", \"type_params\": {}, \"params\": [",
                    fn.name, cfire::lsp::detail::json_escape(rec_str), tparams_str);
                for (std::size_t p = 0; p < fn.params.size(); ++p) {
                    if (p > 0) json += ", ";
                    std::string param_type = cfire::fmt::format_type_ref(fn.params[p].type);
                    std::string intent = fn.params[p].intent.empty() ? "in" : fn.params[p].intent;
                    std::string intent_prefix = (intent != "in") ? (intent + " ") : "";
                    json += std::format("{{\"name\": \"{}\", \"type\": \"{}\", \"intent\": \"{}\", \"signature\": \"{}{}: {}\"}}",
                        fn.params[p].name,
                        cfire::lsp::detail::json_escape(param_type),
                        cfire::lsp::detail::json_escape(intent),
                        intent_prefix, fn.params[p].name, cfire::lsp::detail::json_escape(param_type));
                }
                json += std::format("], \"returns\": \"{}\", \"async\": {}}}",
                    cfire::lsp::detail::json_escape(cfire::fmt::format_type_ref(fn.return_type)),
                    fn.is_async ? "true" : "false");
            }
            json += "\n      ],\n";
            json += "      \"tests\": [\n";
            for (std::size_t i = 0; i < ast->tests.size(); ++i) {
                if (i > 0) json += ",\n";
                const auto& td = ast->tests[i];
                std::string kind = td.is_fuzz ? "fuzz" : (td.is_bench ? "bench" : "test");
                json += std::format("        {{\"name\": \"{}\", \"kind\": \"{}\", \"line\": {}}}",
                    cfire::lsp::detail::json_escape(td.name), kind, td.line);
            }
            json += "\n      ]\n";
            json += "    }";
        }
        json += "\n  }\n}\n";
        std::print("{}", json);
        return 0;
}

[[nodiscard]] inline auto run_inspect_subcommand(
    const std::filesystem::path& target,
    const std::filesystem::path& runtime_dir,
    const std::vector<std::string>& run_args,
    const std::vector<std::string>& extra_includes,
    bool json_mode
) -> int {
        std::string target_str = target.string();

        // Form 2: Foreign C/C++ Header: `cpp <header.h> [@pkg("<lib>")]`
        if (target_str == "cpp" || target_str.starts_with("cpp ")) {
            std::string header;
            if (target_str == "cpp") {
                if (!run_args.empty()) {
                    header = run_args[0];
                }
            } else {
                header = target_str.substr(4);
            }
            while (!header.empty() && std::isspace(static_cast<unsigned char>(header.front()))) header.erase(header.begin());
            while (!header.empty() && std::isspace(static_cast<unsigned char>(header.back()))) header.pop_back();

            if (!header.empty()) {
                std::vector<std::string> cflags;
                for (const auto& inc : extra_includes) {
                    cflags.push_back("-I" + inc);
                }
                auto cache_dir = runtime_dir.parent_path() / ".cfire" / "cache";
                auto idx = cfire::bridge::inspect_header(header, cflags, cache_dir);
                if (!idx) {
                    if (json_mode) {
                        std::println("{{\"success\": false, \"header\": \"{}\", \"error\": \"{}\"}}",
                            cfire::diag::json_escape(header), cfire::diag::json_escape(idx.error()));
                    } else {
                        std::println(stderr, "error inspecting header '{}': {}", header, idx.error());
                    }
                    return 1;
                }

                std::vector<std::pair<std::string, cfire::bridge::CppSymbol>> sorted_syms(idx->symbols.begin(), idx->symbols.end());
                std::sort(sorted_syms.begin(), sorted_syms.end(), [](const auto& a, const auto& b) {
                    return a.first < b.first;
                });

                if (json_mode) {
                    std::string json = std::format("{{\"success\": true, \"header\": \"{}\", \"symbols\": [", cfire::diag::json_escape(header));
                    for (std::size_t i = 0; i < sorted_syms.size(); ++i) {
                        if (i > 0) json += ", ";
                        const auto& sym = sorted_syms[i].second;
                        json += std::format("{{\"name\": \"{}\", \"kind\": \"{}\", \"return_type\": \"{}\", \"params\": [",
                            cfire::diag::json_escape(sym.name),
                            cfire::diag::json_escape(sym.kind),
                            cfire::diag::json_escape(sym.return_type));
                        for (std::size_t p = 0; p < sym.param_types.size(); ++p) {
                            if (p > 0) json += ", ";
                            json += std::format("\"{}\"", cfire::diag::json_escape(sym.param_types[p]));
                        }
                        json += "]}";
                    }
                    json += "]}";
                    std::println("{}", json);
                } else {
                    std::println("Header: <{}> ({} symbols)", header, sorted_syms.size());
                    for (const auto& [sname, sym] : sorted_syms) {
                        std::string params_str;
                        for (std::size_t p = 0; p < sym.param_types.size(); ++p) {
                            if (p > 0) params_str += ", ";
                            params_str += sym.param_types[p];
                        }
                        std::println("  cpp::{}({}) -> {}", sym.name, params_str, sym.return_type.empty() ? "void" : sym.return_type);
                    }
                }
                return 0;
            }
        }

        // Form 1: Source coordinate `<file.cf>:<line>:<col>`
        auto c1 = target_str.rfind(':');
        auto c2 = (c1 != std::string::npos && c1 > 0) ? target_str.rfind(':', c1 - 1) : std::string::npos;
        if (c1 != std::string::npos && c2 != std::string::npos && c2 < c1) {
            auto pline = cfire::detail::parse_int<int>(target_str.substr(c2 + 1, c1 - c2 - 1));
            auto pcol = cfire::detail::parse_int<int>(target_str.substr(c1 + 1));
            if (pline && pcol && *pline > 0 && *pcol > 0) {
                std::string req_file = target_str.substr(0, c2);
                cfire::lsp::Server srv;
                std::filesystem::path p(req_file);
                std::error_code ec;
                auto abs_p = std::filesystem::absolute(p, ec);
                std::string uri = cfire::lsp::detail::path_to_uri(abs_p);
                int lsp_line = std::max(0, *pline - 1);
                int lsp_col = std::max(0, *pcol - 1);
                std::string hover_md = srv.compute_hover(uri, lsp_line, lsp_col);
                std::string def_json = srv.compute_definition(uri, lsp_line, lsp_col);
                std::string refs_json = srv.compute_references(uri, lsp_line, lsp_col);
                cfire::lsp::detail::StringInterpolationSpan interp_span{};
                if (auto raw_txt = cfire::build::read_file_text(abs_p)) {
                    auto lines = cfire::lsp::detail::split_lines(*raw_txt);
                    if (static_cast<std::size_t>(lsp_line) < lines.size()) {
                        interp_span = cfire::lsp::detail::find_string_interpolation_span_at(lines[static_cast<std::size_t>(lsp_line)], lsp_col);
                    }
                }

                if (json_mode) {
                    std::string interp_json = interp_span.in_interp_expr
                        ? std::format("{{\"expr\": \"{}\", \"format_spec\": \"{}\"}}",
                            cfire::diag::json_escape(interp_span.expr_text),
                            cfire::diag::json_escape(interp_span.fmt_spec))
                        : "null";
                    std::println("{{\"target\": \"{}\", \"file\": \"{}\", \"line\": {}, \"col\": {}, \"hover\": \"{}\", \"definition\": {}, \"references\": {}, \"interpolation\": {}}}",
                        cfire::diag::json_escape(target_str),
                        cfire::diag::json_escape(req_file),
                        *pline,
                        *pcol,
                        cfire::diag::json_escape(hover_md),
                        def_json.empty() ? "null" : def_json,
                        refs_json.empty() ? "[]" : refs_json,
                        interp_json);
                } else {
                    std::println("Target: {}:{}:{}", req_file, *pline, *pcol);
                    if (!hover_md.empty()) {
                        std::println("\n{}", hover_md);
                    }
                    if (interp_span.in_interp_expr) {
                        std::println("\nInterpolation: {{{}{}}}",
                            interp_span.expr_text,
                            interp_span.fmt_spec.empty() ? "" : (":" + interp_span.fmt_spec));
                    }
                    if (def_json != "null" && !def_json.empty()) {
                        std::println("\nDefinition:\n{}", def_json);
                    }
                    if (refs_json != "[]" && !refs_json.empty()) {
                        std::println("\nReferences:\n{}", refs_json);
                    }
                }
                return 0;
            }
        }

        // Form 3: `core:<mod>` / `ext:<mod>` / `vendor:<mod>` or `<mod>.<symbol>`
        if (target_str == "log.Level" || target_str == "log::Level") {
            if (json_mode) {
                std::println("{{\"module\": \"log\", \"import_path\": \"core:log\", \"name\": \"Level\", \"signature\": \"type Level enum {{ Debug, Info, Warn, Error }}\", \"doc\": \"Minimum severity threshold for structured logging (cfire::log::Level)\"}}");
            } else {
                std::println("### Enum `log.Level` (`core:log`)\n```cf\ntype Level enum {{\n    Debug = 0\n    Info = 1\n    Warn = 2\n    Error = 3\n}}\n```\n**Lowered C++23:** `cfire::log::Level`");
            }
            return 0;
        }
        if (target_str == "log.Format" || target_str == "log::Format") {
            if (json_mode) {
                std::println("{{\"module\": \"log\", \"import_path\": \"core:log\", \"name\": \"Format\", \"signature\": \"type Format enum {{ Text, Json }}\", \"doc\": \"Output sink format for structured logging (cfire::log::Format)\"}}");
            } else {
                std::println("### Enum `log.Format` (`core:log`)\n```cf\ntype Format enum {{\n    Text = 0\n    Json = 1\n}}\n```\n**Lowered C++23:** `cfire::log::Format`");
            }
            return 0;
        }
        const auto& catalog = cfire::doc::stdlib_catalog();
        auto matches_mod_path = [](const cfire::doc::StdModuleDoc& m, std::string_view q) -> bool {
            if (m.ns_alias == q || m.import_path == q ||
                ("core:" + m.ns_alias) == q || ("ext:" + m.ns_alias) == q || ("vendor:" + m.ns_alias) == q) {
                return true;
            }
            if (q.starts_with("core:") && m.import_path == ("ext:" + std::string(q.substr(5)))) return true;
            if (q.starts_with("ext:") && m.import_path == ("core:" + std::string(q.substr(4)))) return true;
            return false;
        };
        auto dot_pos = target_str.find('.');
        if (dot_pos != std::string::npos && !target_str.starts_with("core:") && !target_str.starts_with("ext:") && !target_str.starts_with("vendor:")) {
            std::string mod_part = target_str.substr(0, dot_pos);
            std::string sym_part = target_str.substr(dot_pos + 1);

            const cfire::doc::StdModuleDoc* found_mod = nullptr;
            for (const auto& m : catalog) {
                if (matches_mod_path(m, mod_part)) {
                    found_mod = &m;
                    break;
                }
            }

            if (found_mod) {
                for (const auto& fn : found_mod->functions) {
                    if (fn.name == sym_part) {
                        if (json_mode) {
                            std::println("{{\"module\": \"{}\", \"import_path\": \"{}\", \"name\": \"{}\", \"signature\": \"{}\", \"doc\": \"{}\", \"example\": \"{}\"}}",
                                cfire::diag::json_escape(found_mod->ns_alias),
                                cfire::diag::json_escape(found_mod->import_path),
                                cfire::diag::json_escape(fn.name),
                                cfire::diag::json_escape(fn.cf_sig),
                                cfire::diag::json_escape(fn.summary),
                                cfire::diag::json_escape(fn.example));
                        } else {
                            std::println("### {}\n\n{}\n\n**Example:**\n```cf\n{}\n```",
                                fn.cf_sig, fn.summary, fn.example);
                        }
                        return 0;
                    }
                }
            }
        }

        const cfire::doc::StdModuleDoc* matched_mod = nullptr;
        for (const auto& m : catalog) {
            if (matches_mod_path(m, target_str)) {
                matched_mod = &m;
                break;
            }
        }

        if (matched_mod) {
            if (json_mode) {
                std::string json = std::format(
                    "{{\"name\": \"{}\", \"import_path\": \"{}\", \"title\": \"{}\", \"overview\": \"{}\", \"functions\": [",
                    cfire::diag::json_escape(matched_mod->ns_alias),
                    cfire::diag::json_escape(matched_mod->import_path),
                    cfire::diag::json_escape(matched_mod->title),
                    cfire::diag::json_escape(matched_mod->overview));
                for (std::size_t i = 0; i < matched_mod->functions.size(); ++i) {
                    if (i > 0) json += ", ";
                    const auto& fn = matched_mod->functions[i];
                    json += std::format("{{\"name\": \"{}\", \"signature\": \"{}\", \"doc\": \"{}\", \"example\": \"{}\"}}",
                        cfire::diag::json_escape(fn.name),
                        cfire::diag::json_escape(fn.cf_sig),
                        cfire::diag::json_escape(fn.summary),
                        cfire::diag::json_escape(fn.example));
                }
                json += "]}";
                std::println("{}", json);
            } else {
                std::println("# {} (import \"{}\")\n\n{}\n\n{}", matched_mod->title, matched_mod->import_path, matched_mod->overview, "## Functions:");
                for (const auto& fn : matched_mod->functions) {
                    std::println("  - {}", fn.cf_sig);
                }
            }
            return 0;
        }

        // Form 4: Diagnostic code lookup (`cfire inspect cfire/USE-AFTER-FREE` or `cfire inspect USE-AFTER-FREE`)
        for (const auto& d : cfire::doc::diagnostics_catalog()) {
            if (d.code == target_str || d.code == ("cfire/" + target_str)) {
                if (json_mode) {
                    std::println(
                        "{{\"code\": \"{}\", \"title\": \"{}\", \"rule\": \"{}\", \"bad_example\": \"{}\", \"good_example\": \"{}\"}}",
                        cfire::diag::json_escape(d.code),
                        cfire::diag::json_escape(d.title),
                        cfire::diag::json_escape(d.rule),
                        cfire::diag::json_escape(d.bad_example),
                        cfire::diag::json_escape(d.good_example));
                } else {
                    std::println("# {} — {}\n\n{}\n\n**Rejected Example:**\n```cf\n{}\n```\n\n**Accepted Fix:**\n```cf\n{}\n```",
                        d.code, d.title, d.rule, d.bad_example, d.good_example);
                }
                return 0;
            }
        }

        std::println(stderr, "error: unknown inspect target '{}'", target_str);
        return 1;
}

} // namespace cfire::cli
